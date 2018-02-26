/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
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


/**
 * \file arrayobj.c
 *
 * Implementation of Vertex Array Objects (VAOs), from OpenGL 3.1+ /
 * the GL_ARB_vertex_array_object extension.
 *
 * \todo
 * The code in this file borrows a lot from bufferobj.c.  There's a certain
 * amount of cruft left over from that origin that may be unnecessary.
 *
 * \author Ian Romanick <idr@us.ibm.com>
 * \author Brian Paul
 */


#include "glheader.h"
#include "hash.h"
#include "image.h"
#include "imports.h"
#include "context.h"
#include "bufferobj.h"
#include "arrayobj.h"
#include "macros.h"
#include "mtypes.h"
#include "state.h"
#include "varray.h"
#include "main/dispatch.h"
#include "util/bitscan.h"
#include "util/u_atomic.h"


const GLubyte
_mesa_vao_attribute_map[ATTRIBUTE_MAP_MODE_MAX][VERT_ATTRIB_MAX] =
{
   /* ATTRIBUTE_MAP_MODE_IDENTITY
    *
    * Grab vertex processing attribute VERT_ATTRIB_POS from
    * the VAO attribute VERT_ATTRIB_POS, and grab vertex processing
    * attribute VERT_ATTRIB_GENERIC0 from the VAO attribute
    * VERT_ATTRIB_GENERIC0.
    */
   {
      VERT_ATTRIB_POS,                 /* VERT_ATTRIB_POS */
      VERT_ATTRIB_NORMAL,              /* VERT_ATTRIB_NORMAL */
      VERT_ATTRIB_COLOR0,              /* VERT_ATTRIB_COLOR0 */
      VERT_ATTRIB_COLOR1,              /* VERT_ATTRIB_COLOR1 */
      VERT_ATTRIB_FOG,                 /* VERT_ATTRIB_FOG */
      VERT_ATTRIB_COLOR_INDEX,         /* VERT_ATTRIB_COLOR_INDEX */
      VERT_ATTRIB_EDGEFLAG,            /* VERT_ATTRIB_EDGEFLAG */
      VERT_ATTRIB_TEX0,                /* VERT_ATTRIB_TEX0 */
      VERT_ATTRIB_TEX1,                /* VERT_ATTRIB_TEX1 */
      VERT_ATTRIB_TEX2,                /* VERT_ATTRIB_TEX2 */
      VERT_ATTRIB_TEX3,                /* VERT_ATTRIB_TEX3 */
      VERT_ATTRIB_TEX4,                /* VERT_ATTRIB_TEX4 */
      VERT_ATTRIB_TEX5,                /* VERT_ATTRIB_TEX5 */
      VERT_ATTRIB_TEX6,                /* VERT_ATTRIB_TEX6 */
      VERT_ATTRIB_TEX7,                /* VERT_ATTRIB_TEX7 */
      VERT_ATTRIB_POINT_SIZE,          /* VERT_ATTRIB_POINT_SIZE */
      VERT_ATTRIB_GENERIC0,            /* VERT_ATTRIB_GENERIC0 */
      VERT_ATTRIB_GENERIC1,            /* VERT_ATTRIB_GENERIC1 */
      VERT_ATTRIB_GENERIC2,            /* VERT_ATTRIB_GENERIC2 */
      VERT_ATTRIB_GENERIC3,            /* VERT_ATTRIB_GENERIC3 */
      VERT_ATTRIB_GENERIC4,            /* VERT_ATTRIB_GENERIC4 */
      VERT_ATTRIB_GENERIC5,            /* VERT_ATTRIB_GENERIC5 */
      VERT_ATTRIB_GENERIC6,            /* VERT_ATTRIB_GENERIC6 */
      VERT_ATTRIB_GENERIC7,            /* VERT_ATTRIB_GENERIC7 */
      VERT_ATTRIB_GENERIC8,            /* VERT_ATTRIB_GENERIC8 */
      VERT_ATTRIB_GENERIC9,            /* VERT_ATTRIB_GENERIC9 */
      VERT_ATTRIB_GENERIC10,           /* VERT_ATTRIB_GENERIC10 */
      VERT_ATTRIB_GENERIC11,           /* VERT_ATTRIB_GENERIC11 */
      VERT_ATTRIB_GENERIC12,           /* VERT_ATTRIB_GENERIC12 */
      VERT_ATTRIB_GENERIC13,           /* VERT_ATTRIB_GENERIC13 */
      VERT_ATTRIB_GENERIC14,           /* VERT_ATTRIB_GENERIC14 */
      VERT_ATTRIB_GENERIC15            /* VERT_ATTRIB_GENERIC15 */
   },

   /* ATTRIBUTE_MAP_MODE_POSITION
    *
    * Grab vertex processing attribute VERT_ATTRIB_POS as well as
    * vertex processing attribute VERT_ATTRIB_GENERIC0 from the
    * VAO attribute VERT_ATTRIB_POS.
    */
   {
      VERT_ATTRIB_POS,                 /* VERT_ATTRIB_POS */
      VERT_ATTRIB_NORMAL,              /* VERT_ATTRIB_NORMAL */
      VERT_ATTRIB_COLOR0,              /* VERT_ATTRIB_COLOR0 */
      VERT_ATTRIB_COLOR1,              /* VERT_ATTRIB_COLOR1 */
      VERT_ATTRIB_FOG,                 /* VERT_ATTRIB_FOG */
      VERT_ATTRIB_COLOR_INDEX,         /* VERT_ATTRIB_COLOR_INDEX */
      VERT_ATTRIB_EDGEFLAG,            /* VERT_ATTRIB_EDGEFLAG */
      VERT_ATTRIB_TEX0,                /* VERT_ATTRIB_TEX0 */
      VERT_ATTRIB_TEX1,                /* VERT_ATTRIB_TEX1 */
      VERT_ATTRIB_TEX2,                /* VERT_ATTRIB_TEX2 */
      VERT_ATTRIB_TEX3,                /* VERT_ATTRIB_TEX3 */
      VERT_ATTRIB_TEX4,                /* VERT_ATTRIB_TEX4 */
      VERT_ATTRIB_TEX5,                /* VERT_ATTRIB_TEX5 */
      VERT_ATTRIB_TEX6,                /* VERT_ATTRIB_TEX6 */
      VERT_ATTRIB_TEX7,                /* VERT_ATTRIB_TEX7 */
      VERT_ATTRIB_POINT_SIZE,          /* VERT_ATTRIB_POINT_SIZE */
      VERT_ATTRIB_POS,                 /* VERT_ATTRIB_GENERIC0 */
      VERT_ATTRIB_GENERIC1,            /* VERT_ATTRIB_GENERIC1 */
      VERT_ATTRIB_GENERIC2,            /* VERT_ATTRIB_GENERIC2 */
      VERT_ATTRIB_GENERIC3,            /* VERT_ATTRIB_GENERIC3 */
      VERT_ATTRIB_GENERIC4,            /* VERT_ATTRIB_GENERIC4 */
      VERT_ATTRIB_GENERIC5,            /* VERT_ATTRIB_GENERIC5 */
      VERT_ATTRIB_GENERIC6,            /* VERT_ATTRIB_GENERIC6 */
      VERT_ATTRIB_GENERIC7,            /* VERT_ATTRIB_GENERIC7 */
      VERT_ATTRIB_GENERIC8,            /* VERT_ATTRIB_GENERIC8 */
      VERT_ATTRIB_GENERIC9,            /* VERT_ATTRIB_GENERIC9 */
      VERT_ATTRIB_GENERIC10,           /* VERT_ATTRIB_GENERIC10 */
      VERT_ATTRIB_GENERIC11,           /* VERT_ATTRIB_GENERIC11 */
      VERT_ATTRIB_GENERIC12,           /* VERT_ATTRIB_GENERIC12 */
      VERT_ATTRIB_GENERIC13,           /* VERT_ATTRIB_GENERIC13 */
      VERT_ATTRIB_GENERIC14,           /* VERT_ATTRIB_GENERIC14 */
      VERT_ATTRIB_GENERIC15            /* VERT_ATTRIB_GENERIC15 */
   },

   /* ATTRIBUTE_MAP_MODE_GENERIC0
    *
    * Grab vertex processing attribute VERT_ATTRIB_POS as well as
    * vertex processing attribute VERT_ATTRIB_GENERIC0 from the
    * VAO attribute VERT_ATTRIB_GENERIC0.
    */
   {
      VERT_ATTRIB_GENERIC0,            /* VERT_ATTRIB_POS */
      VERT_ATTRIB_NORMAL,              /* VERT_ATTRIB_NORMAL */
      VERT_ATTRIB_COLOR0,              /* VERT_ATTRIB_COLOR0 */
      VERT_ATTRIB_COLOR1,              /* VERT_ATTRIB_COLOR1 */
      VERT_ATTRIB_FOG,                 /* VERT_ATTRIB_FOG */
      VERT_ATTRIB_COLOR_INDEX,         /* VERT_ATTRIB_COLOR_INDEX */
      VERT_ATTRIB_EDGEFLAG,            /* VERT_ATTRIB_EDGEFLAG */
      VERT_ATTRIB_TEX0,                /* VERT_ATTRIB_TEX0 */
      VERT_ATTRIB_TEX1,                /* VERT_ATTRIB_TEX1 */
      VERT_ATTRIB_TEX2,                /* VERT_ATTRIB_TEX2 */
      VERT_ATTRIB_TEX3,                /* VERT_ATTRIB_TEX3 */
      VERT_ATTRIB_TEX4,                /* VERT_ATTRIB_TEX4 */
      VERT_ATTRIB_TEX5,                /* VERT_ATTRIB_TEX5 */
      VERT_ATTRIB_TEX6,                /* VERT_ATTRIB_TEX6 */
      VERT_ATTRIB_TEX7,                /* VERT_ATTRIB_TEX7 */
      VERT_ATTRIB_POINT_SIZE,          /* VERT_ATTRIB_POINT_SIZE */
      VERT_ATTRIB_GENERIC0,            /* VERT_ATTRIB_GENERIC0 */
      VERT_ATTRIB_GENERIC1,            /* VERT_ATTRIB_GENERIC1 */
      VERT_ATTRIB_GENERIC2,            /* VERT_ATTRIB_GENERIC2 */
      VERT_ATTRIB_GENERIC3,            /* VERT_ATTRIB_GENERIC3 */
      VERT_ATTRIB_GENERIC4,            /* VERT_ATTRIB_GENERIC4 */
      VERT_ATTRIB_GENERIC5,            /* VERT_ATTRIB_GENERIC5 */
      VERT_ATTRIB_GENERIC6,            /* VERT_ATTRIB_GENERIC6 */
      VERT_ATTRIB_GENERIC7,            /* VERT_ATTRIB_GENERIC7 */
      VERT_ATTRIB_GENERIC8,            /* VERT_ATTRIB_GENERIC8 */
      VERT_ATTRIB_GENERIC9,            /* VERT_ATTRIB_GENERIC9 */
      VERT_ATTRIB_GENERIC10,           /* VERT_ATTRIB_GENERIC10 */
      VERT_ATTRIB_GENERIC11,           /* VERT_ATTRIB_GENERIC11 */
      VERT_ATTRIB_GENERIC12,           /* VERT_ATTRIB_GENERIC12 */
      VERT_ATTRIB_GENERIC13,           /* VERT_ATTRIB_GENERIC13 */
      VERT_ATTRIB_GENERIC14,           /* VERT_ATTRIB_GENERIC14 */
      VERT_ATTRIB_GENERIC15            /* VERT_ATTRIB_GENERIC15 */
   }
};


/**
 * Look up the array object for the given ID.
 *
 * \returns
 * Either a pointer to the array object with the specified ID or \c NULL for
 * a non-existent ID.  The spec defines ID 0 as being technically
 * non-existent.
 */

struct gl_vertex_array_object *
_mesa_lookup_vao(struct gl_context *ctx, GLuint id)
{
   if (id == 0) {
      return NULL;
   } else {
      struct gl_vertex_array_object *vao;

      if (ctx->Array.LastLookedUpVAO &&
          ctx->Array.LastLookedUpVAO->Name == id) {
         vao = ctx->Array.LastLookedUpVAO;
      } else {
         vao = (struct gl_vertex_array_object *)
            _mesa_HashLookupLocked(ctx->Array.Objects, id);

         _mesa_reference_vao(ctx, &ctx->Array.LastLookedUpVAO, vao);
      }

      return vao;
   }
}


/**
 * Looks up the array object for the given ID.
 *
 * Unlike _mesa_lookup_vao, this function generates a GL_INVALID_OPERATION
 * error if the array object does not exist. It also returns the default
 * array object when ctx is a compatibility profile context and id is zero.
 */
struct gl_vertex_array_object *
_mesa_lookup_vao_err(struct gl_context *ctx, GLuint id, const char *caller)
{
   /* The ARB_direct_state_access specification says:
    *
    *    "<vaobj> is [compatibility profile:
    *     zero, indicating the default vertex array object, or]
    *     the name of the vertex array object."
    */
   if (id == 0) {
      if (ctx->API == API_OPENGL_CORE) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(zero is not valid vaobj name in a core profile "
                     "context)", caller);
         return NULL;
      }

      return ctx->Array.DefaultVAO;
   } else {
      struct gl_vertex_array_object *vao;

      if (ctx->Array.LastLookedUpVAO &&
          ctx->Array.LastLookedUpVAO->Name == id) {
         vao = ctx->Array.LastLookedUpVAO;
      } else {
         vao = (struct gl_vertex_array_object *)
            _mesa_HashLookupLocked(ctx->Array.Objects, id);

         /* The ARB_direct_state_access specification says:
          *
          *    "An INVALID_OPERATION error is generated if <vaobj> is not
          *     [compatibility profile: zero or] the name of an existing
          *     vertex array object."
          */
         if (!vao || !vao->EverBound) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(non-existent vaobj=%u)", caller, id);
            return NULL;
         }

         _mesa_reference_vao(ctx, &ctx->Array.LastLookedUpVAO, vao);
      }

      return vao;
   }
}


/**
 * For all the vertex binding points in the array object, unbind any pointers
 * to any buffer objects (VBOs).
 * This is done just prior to array object destruction.
 */
static void
unbind_array_object_vbos(struct gl_context *ctx, struct gl_vertex_array_object *obj)
{
   GLuint i;

   for (i = 0; i < ARRAY_SIZE(obj->BufferBinding); i++)
      _mesa_reference_buffer_object(ctx, &obj->BufferBinding[i].BufferObj, NULL);

   for (i = 0; i < ARRAY_SIZE(obj->_VertexArray); i++)
      _mesa_reference_buffer_object(ctx, &obj->_VertexArray[i].BufferObj, NULL);
}


/**
 * Allocate and initialize a new vertex array object.
 */
struct gl_vertex_array_object *
_mesa_new_vao(struct gl_context *ctx, GLuint name)
{
   struct gl_vertex_array_object *obj = CALLOC_STRUCT(gl_vertex_array_object);
   if (obj)
      _mesa_initialize_vao(ctx, obj, name);
   return obj;
}


/**
 * Delete an array object.
 */
void
_mesa_delete_vao(struct gl_context *ctx, struct gl_vertex_array_object *obj)
{
   unbind_array_object_vbos(ctx, obj);
   _mesa_reference_buffer_object(ctx, &obj->IndexBufferObj, NULL);
   free(obj->Label);
   free(obj);
}


/**
 * Set ptr to vao w/ reference counting.
 * Note: this should only be called from the _mesa_reference_vao()
 * inline function.
 */
void
_mesa_reference_vao_(struct gl_context *ctx,
                     struct gl_vertex_array_object **ptr,
                     struct gl_vertex_array_object *vao)
{
   assert(*ptr != vao);

   if (*ptr) {
      /* Unreference the old array object */
      struct gl_vertex_array_object *oldObj = *ptr;

      bool deleteFlag;
      if (oldObj->SharedAndImmutable) {
         deleteFlag = p_atomic_dec_zero(&oldObj->RefCount);
      } else {
         assert(oldObj->RefCount > 0);
         oldObj->RefCount--;
         deleteFlag = (oldObj->RefCount == 0);
      }

      if (deleteFlag)
         _mesa_delete_vao(ctx, oldObj);

      *ptr = NULL;
   }
   assert(!*ptr);

   if (vao) {
      /* reference new array object */
      if (vao->SharedAndImmutable) {
         p_atomic_inc(&vao->RefCount);
      } else {
         assert(vao->RefCount > 0);
         vao->RefCount++;
      }

      *ptr = vao;
   }
}


/**
 * Initialize attributes of a vertex array within a vertex array object.
 * \param vao  the container vertex array object
 * \param index  which array in the VAO to initialize
 * \param size  number of components (1, 2, 3 or 4) per attribute
 * \param type  datatype of the attribute (GL_FLOAT, GL_INT, etc).
 */
static void
init_array(struct gl_context *ctx,
           struct gl_vertex_array_object *vao,
           gl_vert_attrib index, GLint size, GLint type)
{
   assert(index < ARRAY_SIZE(vao->VertexAttrib));
   struct gl_array_attributes *array = &vao->VertexAttrib[index];
   assert(index < ARRAY_SIZE(vao->BufferBinding));
   struct gl_vertex_buffer_binding *binding = &vao->BufferBinding[index];

   array->Size = size;
   array->Type = type;
   array->Format = GL_RGBA; /* only significant for GL_EXT_vertex_array_bgra */
   array->Stride = 0;
   array->Ptr = NULL;
   array->RelativeOffset = 0;
   array->Enabled = GL_FALSE;
   array->Normalized = GL_FALSE;
   array->Integer = GL_FALSE;
   array->Doubles = GL_FALSE;
   array->_ElementSize = size * _mesa_sizeof_type(type);
   ASSERT_BITFIELD_SIZE(struct gl_array_attributes, BufferBindingIndex,
                        VERT_ATTRIB_MAX - 1);
   array->BufferBindingIndex = index;

   binding->Offset = 0;
   binding->Stride = array->_ElementSize;
   binding->BufferObj = NULL;
   binding->_BoundArrays = BITFIELD_BIT(index);

   /* Vertex array buffers */
   _mesa_reference_buffer_object(ctx, &binding->BufferObj,
                                 ctx->Shared->NullBufferObj);
}


/**
 * Initialize a gl_vertex_array_object's arrays.
 */
void
_mesa_initialize_vao(struct gl_context *ctx,
                     struct gl_vertex_array_object *vao,
                     GLuint name)
{
   GLuint i;

   vao->Name = name;

   vao->RefCount = 1;
   vao->SharedAndImmutable = false;

   /* Init the individual arrays */
   for (i = 0; i < ARRAY_SIZE(vao->VertexAttrib); i++) {
      switch (i) {
      case VERT_ATTRIB_NORMAL:
         init_array(ctx, vao, VERT_ATTRIB_NORMAL, 3, GL_FLOAT);
         break;
      case VERT_ATTRIB_COLOR1:
         init_array(ctx, vao, VERT_ATTRIB_COLOR1, 3, GL_FLOAT);
         break;
      case VERT_ATTRIB_FOG:
         init_array(ctx, vao, VERT_ATTRIB_FOG, 1, GL_FLOAT);
         break;
      case VERT_ATTRIB_COLOR_INDEX:
         init_array(ctx, vao, VERT_ATTRIB_COLOR_INDEX, 1, GL_FLOAT);
         break;
      case VERT_ATTRIB_EDGEFLAG:
         init_array(ctx, vao, VERT_ATTRIB_EDGEFLAG, 1, GL_BOOL);
         break;
      case VERT_ATTRIB_POINT_SIZE:
         init_array(ctx, vao, VERT_ATTRIB_POINT_SIZE, 1, GL_FLOAT);
         break;
      default:
         init_array(ctx, vao, i, 4, GL_FLOAT);
         break;
      }
   }

   vao->_AttributeMapMode = ATTRIBUTE_MAP_MODE_IDENTITY;

   _mesa_reference_buffer_object(ctx, &vao->IndexBufferObj,
                                 ctx->Shared->NullBufferObj);
}


/**
 * Updates the derived gl_vertex_arrays when a gl_array_attributes
 * or a gl_vertex_buffer_binding has changed.
 */
void
_mesa_update_vao_derived_arrays(struct gl_context *ctx,
                                struct gl_vertex_array_object *vao)
{
   GLbitfield arrays = vao->NewArrays;

   /* Make sure we do not run into problems with shared objects */
   assert(!vao->SharedAndImmutable || vao->NewArrays == 0);

   while (arrays) {
      const int attrib = u_bit_scan(&arrays);
      struct gl_vertex_array *array = &vao->_VertexArray[attrib];
      const struct gl_array_attributes *attribs =
         &vao->VertexAttrib[attrib];
      const struct gl_vertex_buffer_binding *buffer_binding =
         &vao->BufferBinding[attribs->BufferBindingIndex];

      _mesa_update_vertex_array(ctx, array, attribs, buffer_binding);
   }
}


void
_mesa_set_vao_immutable(struct gl_context *ctx,
                        struct gl_vertex_array_object *vao)
{
   _mesa_update_vao_derived_arrays(ctx, vao);
   vao->NewArrays = 0;
   vao->SharedAndImmutable = true;
}


bool
_mesa_all_varyings_in_vbos(const struct gl_vertex_array_object *vao)
{
   /* Walk those enabled arrays that have the default vbo attached */
   GLbitfield mask = vao->_Enabled & ~vao->VertexAttribBufferMask;

   while (mask) {
      /* Do not use u_bit_scan64 as we can walk multiple
       * attrib arrays at once
       */
      const int i = ffs(mask) - 1;
      const struct gl_array_attributes *attrib_array =
         &vao->VertexAttrib[i];
      const struct gl_vertex_buffer_binding *buffer_binding =
         &vao->BufferBinding[attrib_array->BufferBindingIndex];

      /* Only enabled arrays shall appear in the _Enabled bitmask */
      assert(attrib_array->Enabled);
      /* We have already masked out vao->VertexAttribBufferMask  */
      assert(!_mesa_is_bufferobj(buffer_binding->BufferObj));

      /* Bail out once we find the first non vbo with a non zero stride */
      if (buffer_binding->Stride != 0)
         return false;

      /* Note that we cannot use the xor variant since the _BoundArray mask
       * may contain array attributes that are bound but not enabled.
       */
      mask &= ~buffer_binding->_BoundArrays;
   }

   return true;
}

bool
_mesa_all_buffers_are_unmapped(const struct gl_vertex_array_object *vao)
{
   /* Walk the enabled arrays that have a vbo attached */
   GLbitfield mask = vao->_Enabled & vao->VertexAttribBufferMask;

   while (mask) {
      const int i = ffs(mask) - 1;
      const struct gl_array_attributes *attrib_array =
         &vao->VertexAttrib[i];
      const struct gl_vertex_buffer_binding *buffer_binding =
         &vao->BufferBinding[attrib_array->BufferBindingIndex];

      /* Only enabled arrays shall appear in the _Enabled bitmask */
      assert(attrib_array->Enabled);
      /* We have already masked with vao->VertexAttribBufferMask  */
      assert(_mesa_is_bufferobj(buffer_binding->BufferObj));

      /* Bail out once we find the first disallowed mapping */
      if (_mesa_check_disallowed_mapping(buffer_binding->BufferObj))
         return false;

      /* We have handled everything that is bound to this buffer_binding. */
      mask &= ~buffer_binding->_BoundArrays;
   }

   return true;
}

/**********************************************************************/
/* API Functions                                                      */
/**********************************************************************/


/**
 * ARB version of glBindVertexArray()
 */
static ALWAYS_INLINE void
bind_vertex_array(struct gl_context *ctx, GLuint id, bool no_error)
{
   struct gl_vertex_array_object *const oldObj = ctx->Array.VAO;
   struct gl_vertex_array_object *newObj = NULL;

   assert(oldObj != NULL);

   if (oldObj->Name == id)
      return;   /* rebinding the same array object- no change */

   /*
    * Get pointer to new array object (newObj)
    */
   if (id == 0) {
      /* The spec says there is no array object named 0, but we use
       * one internally because it simplifies things.
       */
      newObj = ctx->Array.DefaultVAO;
   }
   else {
      /* non-default array object */
      newObj = _mesa_lookup_vao(ctx, id);
      if (!no_error && !newObj) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glBindVertexArray(non-gen name)");
         return;
      }

      newObj->EverBound = GL_TRUE;
   }

   /* The _DrawArrays pointer is pointing at the VAO being unbound and
    * that VAO may be in the process of being deleted. If it's not going
    * to be deleted, this will have no effect, because the pointer needs
    * to be updated by the VBO module anyway.
    *
    * Before the VBO module can update the pointer, we have to set it
    * to NULL for drivers not to set up arrays which are not bound,
    * or to prevent a crash if the VAO being unbound is going to be
    * deleted.
    */
   _mesa_set_drawing_arrays(ctx, NULL);
   _mesa_set_draw_vao(ctx, ctx->Array._EmptyVAO, 0);

   ctx->NewState |= _NEW_ARRAY;
   _mesa_reference_vao(ctx, &ctx->Array.VAO, newObj);
}


void GLAPIENTRY
_mesa_BindVertexArray_no_error(GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   bind_vertex_array(ctx, id, true);
}


void GLAPIENTRY
_mesa_BindVertexArray(GLuint id)
{
   GET_CURRENT_CONTEXT(ctx);
   bind_vertex_array(ctx, id, false);
}


/**
 * Delete a set of array objects.
 *
 * \param n      Number of array objects to delete.
 * \param ids    Array of \c n array object IDs.
 */
static void
delete_vertex_arrays(struct gl_context *ctx, GLsizei n, const GLuint *ids)
{
   GLsizei i;

   for (i = 0; i < n; i++) {
      struct gl_vertex_array_object *obj = _mesa_lookup_vao(ctx, ids[i]);

      if (obj) {
         assert(obj->Name == ids[i]);

         /* If the array object is currently bound, the spec says "the binding
          * for that object reverts to zero and the default vertex array
          * becomes current."
          */
         if (obj == ctx->Array.VAO)
            _mesa_BindVertexArray_no_error(0);

         /* The ID is immediately freed for re-use */
         _mesa_HashRemoveLocked(ctx->Array.Objects, obj->Name);

         if (ctx->Array.LastLookedUpVAO == obj)
            _mesa_reference_vao(ctx, &ctx->Array.LastLookedUpVAO, NULL);
         if (ctx->Array._DrawVAO == obj)
            _mesa_set_draw_vao(ctx, ctx->Array._EmptyVAO, 0);

         /* Unreference the array object. 
          * If refcount hits zero, the object will be deleted.
          */
         _mesa_reference_vao(ctx, &obj, NULL);
      }
   }
}


void GLAPIENTRY
_mesa_DeleteVertexArrays_no_error(GLsizei n, const GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   delete_vertex_arrays(ctx, n, ids);
}


void GLAPIENTRY
_mesa_DeleteVertexArrays(GLsizei n, const GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteVertexArray(n)");
      return;
   }

   delete_vertex_arrays(ctx, n, ids);
}


/**
 * Generate a set of unique array object IDs and store them in \c arrays.
 * Helper for _mesa_GenVertexArrays() and _mesa_CreateVertexArrays()
 * below.
 *
 * \param n       Number of IDs to generate.
 * \param arrays  Array of \c n locations to store the IDs.
 * \param create  Indicates that the objects should also be created.
 * \param func    The name of the GL entry point.
 */
static void
gen_vertex_arrays(struct gl_context *ctx, GLsizei n, GLuint *arrays,
                  bool create, const char *func)
{
   GLuint first;
   GLint i;

   if (!arrays)
      return;

   first = _mesa_HashFindFreeKeyBlock(ctx->Array.Objects, n);

   /* For the sake of simplicity we create the array objects in both
    * the Gen* and Create* cases.  The only difference is the value of
    * EverBound, which is set to true in the Create* case.
    */
   for (i = 0; i < n; i++) {
      struct gl_vertex_array_object *obj;
      GLuint name = first + i;

      obj = _mesa_new_vao(ctx, name);
      if (!obj) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
         return;
      }
      obj->EverBound = create;
      _mesa_HashInsertLocked(ctx->Array.Objects, obj->Name, obj);
      arrays[i] = first + i;
   }
}


static void
gen_vertex_arrays_err(struct gl_context *ctx, GLsizei n, GLuint *arrays,
                      bool create, const char *func)
{
   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   gen_vertex_arrays(ctx, n, arrays, create, func);
}


/**
 * ARB version of glGenVertexArrays()
 * All arrays will be required to live in VBOs.
 */
void GLAPIENTRY
_mesa_GenVertexArrays_no_error(GLsizei n, GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   gen_vertex_arrays(ctx, n, arrays, false, "glGenVertexArrays");
}


void GLAPIENTRY
_mesa_GenVertexArrays(GLsizei n, GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   gen_vertex_arrays_err(ctx, n, arrays, false, "glGenVertexArrays");
}


/**
 * ARB_direct_state_access
 * Generates ID's and creates the array objects.
 */
void GLAPIENTRY
_mesa_CreateVertexArrays_no_error(GLsizei n, GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   gen_vertex_arrays(ctx, n, arrays, true, "glCreateVertexArrays");
}


void GLAPIENTRY
_mesa_CreateVertexArrays(GLsizei n, GLuint *arrays)
{
   GET_CURRENT_CONTEXT(ctx);
   gen_vertex_arrays_err(ctx, n, arrays, true, "glCreateVertexArrays");
}


/**
 * Determine if ID is the name of an array object.
 *
 * \param id  ID of the potential array object.
 * \return  \c GL_TRUE if \c id is the name of a array object,
 *          \c GL_FALSE otherwise.
 */
GLboolean GLAPIENTRY
_mesa_IsVertexArray( GLuint id )
{
   struct gl_vertex_array_object * obj;
   GET_CURRENT_CONTEXT(ctx);
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);

   obj = _mesa_lookup_vao(ctx, id);

   return obj != NULL && obj->EverBound;
}


/**
 * Sets the element array buffer binding of a vertex array object.
 *
 * This is the ARB_direct_state_access equivalent of
 * glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer).
 */
static ALWAYS_INLINE void
vertex_array_element_buffer(struct gl_context *ctx, GLuint vaobj, GLuint buffer,
                            bool no_error)
{
   struct gl_vertex_array_object *vao;
   struct gl_buffer_object *bufObj;

   ASSERT_OUTSIDE_BEGIN_END(ctx);

   if (!no_error) {
      /* The GL_ARB_direct_state_access specification says:
       *
       *    "An INVALID_OPERATION error is generated by
       *     VertexArrayElementBuffer if <vaobj> is not [compatibility profile:
       *     zero or] the name of an existing vertex array object."
       */
      vao =_mesa_lookup_vao_err(ctx, vaobj, "glVertexArrayElementBuffer");
      if (!vao)
         return;
   } else {
      vao = _mesa_lookup_vao(ctx, vaobj);
   }

   if (buffer != 0) {
      if (!no_error) {
         /* The GL_ARB_direct_state_access specification says:
          *
          *    "An INVALID_OPERATION error is generated if <buffer> is not zero
          *     or the name of an existing buffer object."
          */
         bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                             "glVertexArrayElementBuffer");
      } else {
         bufObj = _mesa_lookup_bufferobj(ctx, buffer);
      }
   } else {
      bufObj = ctx->Shared->NullBufferObj;
   }

   if (bufObj)
      _mesa_reference_buffer_object(ctx, &vao->IndexBufferObj, bufObj);
}


void GLAPIENTRY
_mesa_VertexArrayElementBuffer_no_error(GLuint vaobj, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   vertex_array_element_buffer(ctx, vaobj, buffer, true);
}


void GLAPIENTRY
_mesa_VertexArrayElementBuffer(GLuint vaobj, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   vertex_array_element_buffer(ctx, vaobj, buffer, false);
}


void GLAPIENTRY
_mesa_GetVertexArrayiv(GLuint vaobj, GLenum pname, GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_vertex_array_object *vao;

   ASSERT_OUTSIDE_BEGIN_END(ctx);

   /* The GL_ARB_direct_state_access specification says:
    *
    *   "An INVALID_OPERATION error is generated if <vaobj> is not
    *    [compatibility profile: zero or] the name of an existing
    *    vertex array object."
    */
   vao =_mesa_lookup_vao_err(ctx, vaobj, "glGetVertexArrayiv");
   if (!vao)
      return;

   /* The GL_ARB_direct_state_access specification says:
    *
    *   "An INVALID_ENUM error is generated if <pname> is not
    *    ELEMENT_ARRAY_BUFFER_BINDING."
    */
   if (pname != GL_ELEMENT_ARRAY_BUFFER_BINDING) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetVertexArrayiv(pname != "
                  "GL_ELEMENT_ARRAY_BUFFER_BINDING)");
      return;
   }

   param[0] = vao->IndexBufferObj->Name;
}
