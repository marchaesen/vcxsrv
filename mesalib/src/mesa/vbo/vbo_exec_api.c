/**************************************************************************

Copyright 2002-2008 VMware, Inc.

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
 */

#include "main/glheader.h"
#include "main/bufferobj.h"
#include "main/context.h"
#include "main/macros.h"
#include "main/vtxfmt.h"
#include "main/dlist.h"
#include "main/eval.h"
#include "main/state.h"
#include "main/light.h"
#include "main/api_arrayelt.h"
#include "main/draw_validate.h"
#include "main/dispatch.h"
#include "util/bitscan.h"
#include "util/u_memory.h"

#include "vbo_noop.h"
#include "vbo_private.h"


/** ID/name for immediate-mode VBO */
#define IMM_BUFFER_NAME 0xaabbccdd


static void GLAPIENTRY
vbo_exec_Materialfv(GLenum face, GLenum pname, const GLfloat *params);

static void GLAPIENTRY
vbo_exec_EvalCoord1f(GLfloat u);

static void GLAPIENTRY
vbo_exec_EvalCoord2f(GLfloat u, GLfloat v);


static void
vbo_reset_all_attr(struct vbo_exec_context *exec);


/**
 * Close off the last primitive, execute the buffer, restart the
 * primitive.  This is called when we fill a vertex buffer before
 * hitting glEnd.
 */
static void
vbo_exec_wrap_buffers(struct vbo_exec_context *exec)
{
   if (exec->vtx.prim_count == 0) {
      exec->vtx.copied.nr = 0;
      exec->vtx.vert_count = 0;
      exec->vtx.buffer_ptr = exec->vtx.buffer_map;
   }
   else {
      struct _mesa_prim *last_prim = &exec->vtx.prim[exec->vtx.prim_count - 1];
      const GLuint last_begin = last_prim->begin;
      GLuint last_count;

      if (_mesa_inside_begin_end(exec->ctx)) {
         last_prim->count = exec->vtx.vert_count - last_prim->start;
      }

      last_count = last_prim->count;

      /* Special handling for wrapping GL_LINE_LOOP */
      if (last_prim->mode == GL_LINE_LOOP &&
          last_count > 0 &&
          !last_prim->end) {
         /* draw this section of the incomplete line loop as a line strip */
         last_prim->mode = GL_LINE_STRIP;
         if (!last_prim->begin) {
            /* This is not the first section of the line loop, so don't
             * draw the 0th vertex.  We're saving it until we draw the
             * very last section of the loop.
             */
            last_prim->start++;
            last_prim->count--;
         }
      }

      /* Execute the buffer and save copied vertices.
       */
      if (exec->vtx.vert_count)
         vbo_exec_vtx_flush(exec);
      else {
         exec->vtx.prim_count = 0;
         exec->vtx.copied.nr = 0;
      }

      /* Emit a glBegin to start the new list.
       */
      assert(exec->vtx.prim_count == 0);

      if (_mesa_inside_begin_end(exec->ctx)) {
         exec->vtx.prim[0].mode = exec->ctx->Driver.CurrentExecPrimitive;
         exec->vtx.prim[0].begin = 0;
         exec->vtx.prim[0].end = 0;
         exec->vtx.prim[0].start = 0;
         exec->vtx.prim[0].count = 0;
         exec->vtx.prim_count++;

         if (exec->vtx.copied.nr == last_count)
            exec->vtx.prim[0].begin = last_begin;
      }
   }
}


/**
 * Deal with buffer wrapping where provoked by the vertex buffer
 * filling up, as opposed to upgrade_vertex().
 */
static void
vbo_exec_vtx_wrap(struct vbo_exec_context *exec)
{
   unsigned numComponents;

   /* Run pipeline on current vertices, copy wrapped vertices
    * to exec->vtx.copied.
    */
   vbo_exec_wrap_buffers(exec);

   if (!exec->vtx.buffer_ptr) {
      /* probably ran out of memory earlier when allocating the VBO */
      return;
   }

   /* Copy stored stored vertices to start of new list.
    */
   assert(exec->vtx.max_vert - exec->vtx.vert_count > exec->vtx.copied.nr);

   numComponents = exec->vtx.copied.nr * exec->vtx.vertex_size;
   memcpy(exec->vtx.buffer_ptr,
          exec->vtx.copied.buffer,
          numComponents * sizeof(fi_type));
   exec->vtx.buffer_ptr += numComponents;
   exec->vtx.vert_count += exec->vtx.copied.nr;

   exec->vtx.copied.nr = 0;
}


/**
 * Copy the active vertex's values to the ctx->Current fields.
 */
static void
vbo_exec_copy_to_current(struct vbo_exec_context *exec)
{
   struct gl_context *ctx = exec->ctx;
   struct vbo_context *vbo = vbo_context(ctx);
   GLbitfield64 enabled = exec->vtx.enabled & (~BITFIELD64_BIT(VBO_ATTRIB_POS));

   while (enabled) {
      const int i = u_bit_scan64(&enabled);

      /* Note: the exec->vtx.current[i] pointers point into the
       * ctx->Current.Attrib and ctx->Light.Material.Attrib arrays.
       */
      GLfloat *current = (GLfloat *)vbo->current[i].Ptr;
      fi_type tmp[8]; /* space for doubles */
      int dmul = 1;

      if (exec->vtx.attr[i].type == GL_DOUBLE ||
          exec->vtx.attr[i].type == GL_UNSIGNED_INT64_ARB)
         dmul = 2;

      assert(exec->vtx.attr[i].size);

      if (exec->vtx.attr[i].type == GL_DOUBLE ||
          exec->vtx.attr[i].type == GL_UNSIGNED_INT64_ARB) {
         memset(tmp, 0, sizeof(tmp));
         memcpy(tmp, exec->vtx.attrptr[i], exec->vtx.attr[i].size * sizeof(GLfloat));
      } else {
         COPY_CLEAN_4V_TYPE_AS_UNION(tmp,
                                     exec->vtx.attr[i].size,
                                     exec->vtx.attrptr[i],
                                     exec->vtx.attr[i].type);
      }

      if (exec->vtx.attr[i].type != vbo->current[i].Format.Type ||
          memcmp(current, tmp, 4 * sizeof(GLfloat) * dmul) != 0) {
         memcpy(current, tmp, 4 * sizeof(GLfloat) * dmul);

         /* Given that we explicitly state size here, there is no need
          * for the COPY_CLEAN above, could just copy 16 bytes and be
          * done.  The only problem is when Mesa accesses ctx->Current
          * directly.
          */
         /* Size here is in components - not bytes */
         vbo_set_vertex_format(&vbo->current[i].Format,
                               exec->vtx.attr[i].size / dmul,
                               exec->vtx.attr[i].type);

         /* This triggers rather too much recalculation of Mesa state
          * that doesn't get used (eg light positions).
          */
         if (i >= VBO_ATTRIB_MAT_FRONT_AMBIENT &&
             i <= VBO_ATTRIB_MAT_BACK_INDEXES)
            ctx->NewState |= _NEW_LIGHT;

         ctx->NewState |= _NEW_CURRENT_ATTRIB;
      }
   }

   /* Colormaterial -- this kindof sucks.
    */
   if (ctx->Light.ColorMaterialEnabled &&
       exec->vtx.attr[VBO_ATTRIB_COLOR0].size) {
      _mesa_update_color_material(ctx,
                                  ctx->Current.Attrib[VBO_ATTRIB_COLOR0]);
   }
}


/**
 * Flush existing data, set new attrib size, replay copied vertices.
 * This is called when we transition from a small vertex attribute size
 * to a larger one.  Ex: glTexCoord2f -> glTexCoord4f.
 * We need to go back over the previous 2-component texcoords and insert
 * zero and one values.
 * \param attr  VBO_ATTRIB_x vertex attribute value
 */
static void
vbo_exec_wrap_upgrade_vertex(struct vbo_exec_context *exec,
                             GLuint attr, GLuint newSize, GLenum newType)
{
   struct gl_context *ctx = exec->ctx;
   struct vbo_context *vbo = vbo_context(ctx);
   const GLint lastcount = exec->vtx.vert_count;
   fi_type *old_attrptr[VBO_ATTRIB_MAX];
   const GLuint old_vtx_size_no_pos = exec->vtx.vertex_size_no_pos;
   const GLuint old_vtx_size = exec->vtx.vertex_size; /* floats per vertex */
   const GLuint oldSize = exec->vtx.attr[attr].size;
   GLuint i;

   assert(attr < VBO_ATTRIB_MAX);

   /* Run pipeline on current vertices, copy wrapped vertices
    * to exec->vtx.copied.
    */
   vbo_exec_wrap_buffers(exec);

   if (unlikely(exec->vtx.copied.nr)) {
      /* We're in the middle of a primitive, keep the old vertex
       * format around to be able to translate the copied vertices to
       * the new format.
       */
      memcpy(old_attrptr, exec->vtx.attrptr, sizeof(old_attrptr));
   }

   /* Heuristic: Attempt to isolate attributes received outside
    * begin/end so that they don't bloat the vertices.
    */
   if (!_mesa_inside_begin_end(ctx) &&
       !oldSize && lastcount > 8 && exec->vtx.vertex_size) {
      vbo_exec_copy_to_current(exec);
      vbo_reset_all_attr(exec);
   }

   /* Fix up sizes:
    */
   exec->vtx.attr[attr].size = newSize;
   exec->vtx.attr[attr].active_size = newSize;
   exec->vtx.attr[attr].type = newType;
   exec->vtx.vertex_size += newSize - oldSize;
   exec->vtx.vertex_size_no_pos = exec->vtx.vertex_size - exec->vtx.attr[0].size;
   exec->vtx.max_vert = vbo_compute_max_verts(exec);
   exec->vtx.vert_count = 0;
   exec->vtx.buffer_ptr = exec->vtx.buffer_map;
   exec->vtx.enabled |= BITFIELD64_BIT(attr);

   if (attr != 0) {
      if (unlikely(oldSize)) {
         unsigned offset = exec->vtx.attrptr[attr] - exec->vtx.vertex;

         /* If there are attribs after the resized attrib... */
         if (offset + oldSize < old_vtx_size_no_pos) {
            int size_diff = newSize - oldSize;
            fi_type *old_first = exec->vtx.attrptr[attr] + oldSize;
            fi_type *new_first = exec->vtx.attrptr[attr] + newSize;
            fi_type *old_last = exec->vtx.vertex + old_vtx_size_no_pos - 1;
            fi_type *new_last = exec->vtx.vertex + exec->vtx.vertex_size_no_pos - 1;

            if (size_diff < 0) {
               /* Decreasing the size: Copy from first to last to move
                * elements to the left.
                */
               fi_type *old_end = old_last + 1;
               fi_type *old = old_first;
               fi_type *new = new_first;

               do {
                  *new++ = *old++;
               } while (old != old_end);
            } else {
               /* Increasing the size: Copy from last to first to move
                * elements to the right.
                */
               fi_type *old_end = old_first - 1;
               fi_type *old = old_last;
               fi_type *new = new_last;

               do {
                  *new-- = *old--;
               } while (old != old_end);
            }

            /* Update pointers to attribs, because we moved them. */
            GLbitfield64 enabled = exec->vtx.enabled &
                                   ~BITFIELD64_BIT(VBO_ATTRIB_POS) &
                                   ~BITFIELD64_BIT(attr);
            while (enabled) {
               unsigned i = u_bit_scan64(&enabled);

               if (exec->vtx.attrptr[i] > exec->vtx.attrptr[attr])
                  exec->vtx.attrptr[i] += size_diff;
            }
         }
      } else {
         /* Just have to append the new attribute at the end */
         exec->vtx.attrptr[attr] = exec->vtx.vertex +
           exec->vtx.vertex_size_no_pos - newSize;
      }
   }

   /* The position is always last. */
   exec->vtx.attrptr[0] = exec->vtx.vertex + exec->vtx.vertex_size_no_pos;

   /* Replay stored vertices to translate them
    * to new format here.
    *
    * -- No need to replay - just copy piecewise
    */
   if (unlikely(exec->vtx.copied.nr)) {
      fi_type *data = exec->vtx.copied.buffer;
      fi_type *dest = exec->vtx.buffer_ptr;

      assert(exec->vtx.buffer_ptr == exec->vtx.buffer_map);

      for (i = 0 ; i < exec->vtx.copied.nr ; i++) {
         GLbitfield64 enabled = exec->vtx.enabled;
         while (enabled) {
            const int j = u_bit_scan64(&enabled);
            GLuint sz = exec->vtx.attr[j].size;
            GLint old_offset = old_attrptr[j] - exec->vtx.vertex;
            GLint new_offset = exec->vtx.attrptr[j] - exec->vtx.vertex;

            assert(sz);

            if (j == attr) {
               if (oldSize) {
                  fi_type tmp[4];
                  COPY_CLEAN_4V_TYPE_AS_UNION(tmp, oldSize,
                                              data + old_offset,
                                              exec->vtx.attr[j].type);
                  COPY_SZ_4V(dest + new_offset, newSize, tmp);
               } else {
                  fi_type *current = (fi_type *)vbo->current[j].Ptr;
                  COPY_SZ_4V(dest + new_offset, sz, current);
               }
            }
            else {
               COPY_SZ_4V(dest + new_offset, sz, data + old_offset);
            }
         }

         data += old_vtx_size;
         dest += exec->vtx.vertex_size;
      }

      exec->vtx.buffer_ptr = dest;
      exec->vtx.vert_count += exec->vtx.copied.nr;
      exec->vtx.copied.nr = 0;
   }
}


/**
 * This is when a vertex attribute transitions to a different size.
 * For example, we saw a bunch of glTexCoord2f() calls and now we got a
 * glTexCoord4f() call.  We promote the array from size=2 to size=4.
 * \param newSize  size of new vertex (number of 32-bit words).
 * \param attr  VBO_ATTRIB_x vertex attribute value
 */
static void
vbo_exec_fixup_vertex(struct gl_context *ctx, GLuint attr,
                      GLuint newSize, GLenum newType)
{
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   assert(attr < VBO_ATTRIB_MAX);

   if (newSize > exec->vtx.attr[attr].size ||
       newType != exec->vtx.attr[attr].type) {
      /* New size is larger.  Need to flush existing vertices and get
       * an enlarged vertex format.
       */
      vbo_exec_wrap_upgrade_vertex(exec, attr, newSize, newType);
   }
   else if (newSize < exec->vtx.attr[attr].active_size) {
      GLuint i;
      const fi_type *id =
            vbo_get_default_vals_as_union(exec->vtx.attr[attr].type);

      /* New size is smaller - just need to fill in some
       * zeros.  Don't need to flush or wrap.
       */
      for (i = newSize; i <= exec->vtx.attr[attr].size; i++)
         exec->vtx.attrptr[attr][i-1] = id[i-1];

      exec->vtx.attr[attr].active_size = newSize;
   }
}


/**
 * If index=0, does glVertexAttrib*() alias glVertex() to emit a vertex?
 * It depends on a few things, including whether we're inside or outside
 * of glBegin/glEnd.
 */
static inline bool
is_vertex_position(const struct gl_context *ctx, GLuint index)
{
   return (index == 0 &&
           _mesa_attr_zero_aliases_vertex(ctx) &&
           _mesa_inside_begin_end(ctx));
}

/* Write a 64-bit value into a 32-bit pointer by preserving endianness. */
#if UTIL_ARCH_LITTLE_ENDIAN
   #define SET_64BIT(dst32, u64) do { \
         *(dst32)++ = (u64); \
         *(dst32)++ = (uint64_t)(u64) >> 32; \
      } while (0)
#else
   #define SET_64BIT(dst32, u64) do { \
         *(dst32)++ = (uint64_t)(u64) >> 32; \
         *(dst32)++ = (u64); \
      } while (0)
#endif


/**
 * This macro is used to implement all the glVertex, glColor, glTexCoord,
 * glVertexAttrib, etc functions.
 * \param A  VBO_ATTRIB_x attribute index
 * \param N  attribute size (1..4)
 * \param T  type (GL_FLOAT, GL_DOUBLE, GL_INT, GL_UNSIGNED_INT)
 * \param C  cast type (uint32_t or uint64_t)
 * \param V0, V1, v2, V3  attribute value
 */
#define ATTR_UNION(A, N, T, C, V0, V1, V2, V3)                          \
do {                                                                    \
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;             \
   int sz = (sizeof(C) / sizeof(GLfloat));                              \
                                                                        \
   assert(sz == 1 || sz == 2);                                          \
                                                                        \
   /* store a copy of the attribute in exec except for glVertex */      \
   if ((A) != 0) {                                                      \
      /* Check if attribute size or type is changing. */                \
      if (unlikely(exec->vtx.attr[A].active_size != N * sz ||           \
                   exec->vtx.attr[A].type != T)) {                      \
         vbo_exec_fixup_vertex(ctx, A, N * sz, T);                      \
      }                                                                 \
                                                                        \
      C *dest = (C *)exec->vtx.attrptr[A];                              \
      if (N>0) dest[0] = V0;                                            \
      if (N>1) dest[1] = V1;                                            \
      if (N>2) dest[2] = V2;                                            \
      if (N>3) dest[3] = V3;                                            \
      assert(exec->vtx.attr[A].type == T);                              \
                                                                        \
      /* we now have accumulated a per-vertex attribute */              \
      ctx->Driver.NeedFlush |= FLUSH_UPDATE_CURRENT;                    \
   } else {                                                             \
      /* This is a glVertex call */                                     \
      int size = exec->vtx.attr[0].size;                                \
                                                                        \
      /* Check if attribute size or type is changing. */                \
      if (unlikely(size < N * sz ||                                     \
                   exec->vtx.attr[0].type != T)) {                      \
         vbo_exec_wrap_upgrade_vertex(exec, 0, N * sz, T);              \
      }                                                                 \
                                                                        \
      uint32_t *dst = (uint32_t *)exec->vtx.buffer_ptr;                 \
      uint32_t *src = (uint32_t *)exec->vtx.vertex;                     \
      unsigned vertex_size_no_pos = exec->vtx.vertex_size_no_pos;       \
                                                                        \
      /* Copy over attributes from exec. */                             \
      for (unsigned i = 0; i < vertex_size_no_pos; i++)                 \
         *dst++ = *src++;                                               \
                                                                        \
      /* Store the position, which is always last and can have 32 or */ \
      /* 64 bits per channel. */                                        \
      if (sizeof(C) == 4) {                                             \
         if (N > 0) *dst++ = V0;                                        \
         if (N > 1) *dst++ = V1;                                        \
         if (N > 2) *dst++ = V2;                                        \
         if (N > 3) *dst++ = V3;                                        \
                                                                        \
         if (unlikely(N < size)) {                                      \
            if (N < 2 && size >= 2) *dst++ = V1;                        \
            if (N < 3 && size >= 3) *dst++ = V2;                        \
            if (N < 4 && size >= 4) *dst++ = V3;                        \
         }                                                              \
      } else {                                                          \
         /* 64 bits: dst can be unaligned, so copy each 4-byte word */  \
         /* separately */                                               \
         if (N > 0) SET_64BIT(dst, V0);                                 \
         if (N > 1) SET_64BIT(dst, V1);                                 \
         if (N > 2) SET_64BIT(dst, V2);                                 \
         if (N > 3) SET_64BIT(dst, V3);                                 \
                                                                        \
         if (unlikely(N * 2 < size)) {                                  \
            if (N < 2 && size >= 4) SET_64BIT(dst, V1);                 \
            if (N < 3 && size >= 6) SET_64BIT(dst, V2);                 \
            if (N < 4 && size >= 8) SET_64BIT(dst, V3);                 \
         }                                                              \
      }                                                                 \
                                                                        \
      /* dst now points at the beginning of the next vertex */          \
      exec->vtx.buffer_ptr = (fi_type*)dst;                             \
                                                                        \
      /* Don't set FLUSH_UPDATE_CURRENT because */                      \
      /* Current.Attrib[VBO_ATTRIB_POS] is never used. */               \
                                                                        \
      if (unlikely(++exec->vtx.vert_count >= exec->vtx.max_vert))       \
         vbo_exec_vtx_wrap(exec);                                       \
   }                                                                    \
} while (0)


#undef ERROR
#define ERROR(err) _mesa_error(ctx, err, __func__)
#define TAG(x) vbo_exec_##x

#include "vbo_attrib_tmp.h"



/**
 * Execute a glMaterial call.  Note that if GL_COLOR_MATERIAL is enabled,
 * this may be a (partial) no-op.
 */
static void GLAPIENTRY
vbo_exec_Materialfv(GLenum face, GLenum pname, const GLfloat *params)
{
   GLbitfield updateMats;
   GET_CURRENT_CONTEXT(ctx);

   /* This function should be a no-op when it tries to update material
    * attributes which are currently tracking glColor via glColorMaterial.
    * The updateMats var will be a mask of the MAT_BIT_FRONT/BACK_x bits
    * indicating which material attributes can actually be updated below.
    */
   if (ctx->Light.ColorMaterialEnabled) {
      updateMats = ~ctx->Light._ColorMaterialBitmask;
   }
   else {
      /* GL_COLOR_MATERIAL is disabled so don't skip any material updates */
      updateMats = ALL_MATERIAL_BITS;
   }

   if (ctx->API == API_OPENGL_COMPAT && face == GL_FRONT) {
      updateMats &= FRONT_MATERIAL_BITS;
   }
   else if (ctx->API == API_OPENGL_COMPAT && face == GL_BACK) {
      updateMats &= BACK_MATERIAL_BITS;
   }
   else if (face != GL_FRONT_AND_BACK) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glMaterial(invalid face)");
      return;
   }

   switch (pname) {
   case GL_EMISSION:
      if (updateMats & MAT_BIT_FRONT_EMISSION)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_EMISSION, 4, params);
      if (updateMats & MAT_BIT_BACK_EMISSION)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_EMISSION, 4, params);
      break;
   case GL_AMBIENT:
      if (updateMats & MAT_BIT_FRONT_AMBIENT)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_AMBIENT, 4, params);
      if (updateMats & MAT_BIT_BACK_AMBIENT)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_AMBIENT, 4, params);
      break;
   case GL_DIFFUSE:
      if (updateMats & MAT_BIT_FRONT_DIFFUSE)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_DIFFUSE, 4, params);
      if (updateMats & MAT_BIT_BACK_DIFFUSE)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_DIFFUSE, 4, params);
      break;
   case GL_SPECULAR:
      if (updateMats & MAT_BIT_FRONT_SPECULAR)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_SPECULAR, 4, params);
      if (updateMats & MAT_BIT_BACK_SPECULAR)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_SPECULAR, 4, params);
      break;
   case GL_SHININESS:
      if (*params < 0 || *params > ctx->Const.MaxShininess) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glMaterial(invalid shininess: %f out range [0, %f])",
                     *params, ctx->Const.MaxShininess);
         return;
      }
      if (updateMats & MAT_BIT_FRONT_SHININESS)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_SHININESS, 1, params);
      if (updateMats & MAT_BIT_BACK_SHININESS)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_SHININESS, 1, params);
      break;
   case GL_COLOR_INDEXES:
      if (ctx->API != API_OPENGL_COMPAT) {
         _mesa_error(ctx, GL_INVALID_ENUM, "glMaterialfv(pname)");
         return;
      }
      if (updateMats & MAT_BIT_FRONT_INDEXES)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_INDEXES, 3, params);
      if (updateMats & MAT_BIT_BACK_INDEXES)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_INDEXES, 3, params);
      break;
   case GL_AMBIENT_AND_DIFFUSE:
      if (updateMats & MAT_BIT_FRONT_AMBIENT)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_AMBIENT, 4, params);
      if (updateMats & MAT_BIT_FRONT_DIFFUSE)
         MAT_ATTR(VBO_ATTRIB_MAT_FRONT_DIFFUSE, 4, params);
      if (updateMats & MAT_BIT_BACK_AMBIENT)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_AMBIENT, 4, params);
      if (updateMats & MAT_BIT_BACK_DIFFUSE)
         MAT_ATTR(VBO_ATTRIB_MAT_BACK_DIFFUSE, 4, params);
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glMaterialfv(pname)");
      return;
   }
}


/**
 * Flush (draw) vertices.
 *
 * \param flags  bitmask of FLUSH_STORED_VERTICES, FLUSH_UPDATE_CURRENT
 */
static void
vbo_exec_FlushVertices_internal(struct vbo_exec_context *exec, unsigned flags)
{
   struct gl_context *ctx = exec->ctx;

   if (flags & FLUSH_STORED_VERTICES) {
      if (exec->vtx.vert_count) {
         vbo_exec_vtx_flush(exec);
      }

      if (exec->vtx.vertex_size) {
         vbo_exec_copy_to_current(exec);
         vbo_reset_all_attr(exec);
      }

      /* All done. */
      ctx->Driver.NeedFlush = 0;
   } else {
      assert(flags == FLUSH_UPDATE_CURRENT);

      /* Note that the vertex size is unchanged.
       * (vbo_reset_all_attr isn't called)
       */
      vbo_exec_copy_to_current(exec);

      /* Only FLUSH_UPDATE_CURRENT is done. */
      ctx->Driver.NeedFlush = ~FLUSH_UPDATE_CURRENT;
   }
}


static void GLAPIENTRY
vbo_exec_EvalCoord1f(GLfloat u)
{
   GET_CURRENT_CONTEXT(ctx);
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   {
      GLint i;
      if (exec->eval.recalculate_maps)
         vbo_exec_eval_update(exec);

      for (i = 0; i <= VBO_ATTRIB_TEX7; i++) {
         if (exec->eval.map1[i].map)
            if (exec->vtx.attr[i].active_size != exec->eval.map1[i].sz)
               vbo_exec_fixup_vertex(ctx, i, exec->eval.map1[i].sz, GL_FLOAT);
      }
   }

   memcpy(exec->vtx.copied.buffer, exec->vtx.vertex,
          exec->vtx.vertex_size * sizeof(GLfloat));

   vbo_exec_do_EvalCoord1f(exec, u);

   memcpy(exec->vtx.vertex, exec->vtx.copied.buffer,
          exec->vtx.vertex_size * sizeof(GLfloat));
}


static void GLAPIENTRY
vbo_exec_EvalCoord2f(GLfloat u, GLfloat v)
{
   GET_CURRENT_CONTEXT(ctx);
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   {
      GLint i;
      if (exec->eval.recalculate_maps)
         vbo_exec_eval_update(exec);

      for (i = 0; i <= VBO_ATTRIB_TEX7; i++) {
         if (exec->eval.map2[i].map)
            if (exec->vtx.attr[i].active_size != exec->eval.map2[i].sz)
               vbo_exec_fixup_vertex(ctx, i, exec->eval.map2[i].sz, GL_FLOAT);
      }

      if (ctx->Eval.AutoNormal)
         if (exec->vtx.attr[VBO_ATTRIB_NORMAL].active_size != 3)
            vbo_exec_fixup_vertex(ctx, VBO_ATTRIB_NORMAL, 3, GL_FLOAT);
   }

   memcpy(exec->vtx.copied.buffer, exec->vtx.vertex,
          exec->vtx.vertex_size * sizeof(GLfloat));

   vbo_exec_do_EvalCoord2f(exec, u, v);

   memcpy(exec->vtx.vertex, exec->vtx.copied.buffer,
          exec->vtx.vertex_size * sizeof(GLfloat));
}


static void GLAPIENTRY
vbo_exec_EvalCoord1fv(const GLfloat *u)
{
   vbo_exec_EvalCoord1f(u[0]);
}


static void GLAPIENTRY
vbo_exec_EvalCoord2fv(const GLfloat *u)
{
   vbo_exec_EvalCoord2f(u[0], u[1]);
}


static void GLAPIENTRY
vbo_exec_EvalPoint1(GLint i)
{
   GET_CURRENT_CONTEXT(ctx);
   GLfloat du = ((ctx->Eval.MapGrid1u2 - ctx->Eval.MapGrid1u1) /
                 (GLfloat) ctx->Eval.MapGrid1un);
   GLfloat u = i * du + ctx->Eval.MapGrid1u1;

   vbo_exec_EvalCoord1f(u);
}


static void GLAPIENTRY
vbo_exec_EvalPoint2(GLint i, GLint j)
{
   GET_CURRENT_CONTEXT(ctx);
   GLfloat du = ((ctx->Eval.MapGrid2u2 - ctx->Eval.MapGrid2u1) /
                 (GLfloat) ctx->Eval.MapGrid2un);
   GLfloat dv = ((ctx->Eval.MapGrid2v2 - ctx->Eval.MapGrid2v1) /
                 (GLfloat) ctx->Eval.MapGrid2vn);
   GLfloat u = i * du + ctx->Eval.MapGrid2u1;
   GLfloat v = j * dv + ctx->Eval.MapGrid2v1;

   vbo_exec_EvalCoord2f(u, v);
}


/**
 * Called via glBegin.
 */
static void GLAPIENTRY
vbo_exec_Begin(GLenum mode)
{
   GET_CURRENT_CONTEXT(ctx);
   struct vbo_context *vbo = vbo_context(ctx);
   struct vbo_exec_context *exec = &vbo->exec;
   int i;

   if (_mesa_inside_begin_end(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glBegin");
      return;
   }

   if (!_mesa_valid_prim_mode(ctx, mode, "glBegin")) {
      return;
   }

   if (!_mesa_valid_to_render(ctx, "glBegin")) {
      return;
   }

   /* Heuristic: attempt to isolate attributes occurring outside
    * begin/end pairs.
    *
    * Use FLUSH_STORED_VERTICES, because it updates current attribs and
    * sets vertex_size to 0. (FLUSH_UPDATE_CURRENT doesn't change vertex_size)
    */
   if (exec->vtx.vertex_size && !exec->vtx.attr[VBO_ATTRIB_POS].size)
      vbo_exec_FlushVertices_internal(exec, FLUSH_STORED_VERTICES);

   i = exec->vtx.prim_count++;
   exec->vtx.prim[i].mode = mode;
   exec->vtx.prim[i].begin = 1;
   exec->vtx.prim[i].end = 0;
   exec->vtx.prim[i].start = exec->vtx.vert_count;
   exec->vtx.prim[i].count = 0;

   ctx->Driver.CurrentExecPrimitive = mode;

   ctx->Exec = ctx->BeginEnd;

   /* We may have been called from a display list, in which case we should
    * leave dlist.c's dispatch table in place.
    */
   if (ctx->CurrentClientDispatch == ctx->MarshalExec) {
      ctx->CurrentServerDispatch = ctx->Exec;
   } else if (ctx->CurrentClientDispatch == ctx->OutsideBeginEnd) {
      ctx->CurrentClientDispatch = ctx->Exec;
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
   } else {
      assert(ctx->CurrentClientDispatch == ctx->Save);
   }
}


/**
 * Try to merge / concatenate the two most recent VBO primitives.
 */
static void
try_vbo_merge(struct vbo_exec_context *exec)
{
   struct _mesa_prim *cur =  &exec->vtx.prim[exec->vtx.prim_count - 1];

   assert(exec->vtx.prim_count >= 1);

   vbo_try_prim_conversion(cur);

   if (exec->vtx.prim_count >= 2) {
      struct _mesa_prim *prev = &exec->vtx.prim[exec->vtx.prim_count - 2];
      assert(prev == cur - 1);

      if (vbo_merge_draws(exec->ctx, false, prev, cur))
         exec->vtx.prim_count--;  /* drop the last primitive */
   }
}


/**
 * Called via glEnd.
 */
static void GLAPIENTRY
vbo_exec_End(void)
{
   GET_CURRENT_CONTEXT(ctx);
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   if (!_mesa_inside_begin_end(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glEnd");
      return;
   }

   ctx->Exec = ctx->OutsideBeginEnd;

   if (ctx->CurrentClientDispatch == ctx->MarshalExec) {
      ctx->CurrentServerDispatch = ctx->Exec;
   } else if (ctx->CurrentClientDispatch == ctx->BeginEnd) {
      ctx->CurrentClientDispatch = ctx->Exec;
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
   }

   if (exec->vtx.prim_count > 0) {
      /* close off current primitive */
      struct _mesa_prim *last_prim = &exec->vtx.prim[exec->vtx.prim_count - 1];
      unsigned count = exec->vtx.vert_count - last_prim->start;

      last_prim->end = 1;
      last_prim->count = count;

      if (count)
         ctx->Driver.NeedFlush |= FLUSH_STORED_VERTICES;

      /* Special handling for GL_LINE_LOOP */
      if (last_prim->mode == GL_LINE_LOOP && last_prim->begin == 0) {
         /* We're finishing drawing a line loop.  Append 0th vertex onto
          * end of vertex buffer so we can draw it as a line strip.
          */
         const fi_type *src = exec->vtx.buffer_map +
            last_prim->start * exec->vtx.vertex_size;
         fi_type *dst = exec->vtx.buffer_map +
            exec->vtx.vert_count * exec->vtx.vertex_size;

         /* copy 0th vertex to end of buffer */
         memcpy(dst, src, exec->vtx.vertex_size * sizeof(fi_type));

         last_prim->start++;  /* skip vertex0 */
         /* note that last_prim->count stays unchanged */
         last_prim->mode = GL_LINE_STRIP;

         /* Increment the vertex count so the next primitive doesn't
          * overwrite the last vertex which we just added.
          */
         exec->vtx.vert_count++;
         exec->vtx.buffer_ptr += exec->vtx.vertex_size;
      }

      try_vbo_merge(exec);
   }

   ctx->Driver.CurrentExecPrimitive = PRIM_OUTSIDE_BEGIN_END;

   if (exec->vtx.prim_count == VBO_MAX_PRIM)
      vbo_exec_vtx_flush(exec);

   if (MESA_DEBUG_FLAGS & DEBUG_ALWAYS_FLUSH) {
      _mesa_flush(ctx);
   }
}


/**
 * Called via glPrimitiveRestartNV()
 */
static void GLAPIENTRY
vbo_exec_PrimitiveRestartNV(void)
{
   GLenum curPrim;
   GET_CURRENT_CONTEXT(ctx);

   curPrim = ctx->Driver.CurrentExecPrimitive;

   if (curPrim == PRIM_OUTSIDE_BEGIN_END) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glPrimitiveRestartNV");
   }
   else {
      vbo_exec_End();
      vbo_exec_Begin(curPrim);
   }
}


static void
vbo_exec_vtxfmt_init(struct vbo_exec_context *exec)
{
   struct gl_context *ctx = exec->ctx;
   GLvertexformat *vfmt = &exec->vtxfmt;

#define NAME_AE(x) _ae_##x
#define NAME_CALLLIST(x) _mesa_##x
#define NAME(x) vbo_exec_##x
#define NAME_ES(x) _es_##x

#include "vbo_init_tmp.h"
}


static void
vbo_reset_all_attr(struct vbo_exec_context *exec)
{
   while (exec->vtx.enabled) {
      const int i = u_bit_scan64(&exec->vtx.enabled);

      /* Reset the vertex attribute by setting its size to zero. */
      exec->vtx.attr[i].size = 0;
      exec->vtx.attr[i].type = GL_FLOAT;
      exec->vtx.attr[i].active_size = 0;
      exec->vtx.attrptr[i] = NULL;
   }

   exec->vtx.vertex_size = 0;
}


void
vbo_exec_vtx_init(struct vbo_exec_context *exec, bool use_buffer_objects)
{
   struct gl_context *ctx = exec->ctx;

   if (use_buffer_objects) {
      /* Use buffer objects for immediate mode. */
      struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

      exec->vtx.bufferobj = ctx->Driver.NewBufferObject(ctx, IMM_BUFFER_NAME);

      /* Map the buffer. */
      vbo_exec_vtx_map(exec);
      assert(exec->vtx.buffer_ptr);
   } else {
      /* Use allocated memory for immediate mode. */
      exec->vtx.bufferobj = NULL;
      exec->vtx.buffer_map =
         align_malloc(ctx->Const.glBeginEndBufferSize, 64);
      exec->vtx.buffer_ptr = exec->vtx.buffer_map;
   }

   vbo_exec_vtxfmt_init(exec);
   _mesa_noop_vtxfmt_init(ctx, &exec->vtxfmt_noop);

   exec->vtx.enabled = u_bit_consecutive64(0, VBO_ATTRIB_MAX); /* reset all */
   vbo_reset_all_attr(exec);
}


void
vbo_exec_vtx_destroy(struct vbo_exec_context *exec)
{
   /* using a real VBO for vertex data */
   struct gl_context *ctx = exec->ctx;

   /* True VBOs should already be unmapped
    */
   if (exec->vtx.buffer_map) {
      assert(!exec->vtx.bufferobj ||
             exec->vtx.bufferobj->Name == IMM_BUFFER_NAME);
      if (!exec->vtx.bufferobj) {
         align_free(exec->vtx.buffer_map);
         exec->vtx.buffer_map = NULL;
         exec->vtx.buffer_ptr = NULL;
      }
   }

   /* Free the vertex buffer.  Unmap first if needed.
    */
   if (exec->vtx.bufferobj &&
       _mesa_bufferobj_mapped(exec->vtx.bufferobj, MAP_INTERNAL)) {
      ctx->Driver.UnmapBuffer(ctx, exec->vtx.bufferobj, MAP_INTERNAL);
   }
   _mesa_reference_buffer_object(ctx, &exec->vtx.bufferobj, NULL);
}


/**
 * If inside glBegin()/glEnd(), it should assert(0).  Otherwise, if
 * FLUSH_STORED_VERTICES bit in \p flags is set flushes any buffered
 * vertices, if FLUSH_UPDATE_CURRENT bit is set updates
 * __struct gl_contextRec::Current and gl_light_attrib::Material
 *
 * Note that the default T&L engine never clears the
 * FLUSH_UPDATE_CURRENT bit, even after performing the update.
 *
 * \param flags  bitmask of FLUSH_STORED_VERTICES, FLUSH_UPDATE_CURRENT
 */
void
vbo_exec_FlushVertices(struct gl_context *ctx, GLuint flags)
{
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

#ifndef NDEBUG
   /* debug check: make sure we don't get called recursively */
   exec->flush_call_depth++;
   assert(exec->flush_call_depth == 1);
#endif

   if (_mesa_inside_begin_end(ctx)) {
      /* We've had glBegin but not glEnd! */
#ifndef NDEBUG
      exec->flush_call_depth--;
      assert(exec->flush_call_depth == 0);
#endif
      return;
   }

   /* Flush (draw). */
   vbo_exec_FlushVertices_internal(exec, flags);

#ifndef NDEBUG
   exec->flush_call_depth--;
   assert(exec->flush_call_depth == 0);
#endif
}


void GLAPIENTRY
_es_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
   vbo_exec_Color4f(r, g, b, a);
}


void GLAPIENTRY
_es_Normal3f(GLfloat x, GLfloat y, GLfloat z)
{
   vbo_exec_Normal3f(x, y, z);
}


void GLAPIENTRY
_es_MultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
   vbo_exec_MultiTexCoord4f(target, s, t, r, q);
}


void GLAPIENTRY
_es_Materialfv(GLenum face, GLenum pname, const GLfloat *params)
{
   vbo_exec_Materialfv(face, pname, params);
}


void GLAPIENTRY
_es_Materialf(GLenum face, GLenum pname, GLfloat param)
{
   GLfloat p[4];
   p[0] = param;
   p[1] = p[2] = p[3] = 0.0F;
   vbo_exec_Materialfv(face, pname, p);
}


/**
 * A special version of glVertexAttrib4f that does not treat index 0 as
 * VBO_ATTRIB_POS.
 */
static void
VertexAttrib4f_nopos(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   GET_CURRENT_CONTEXT(ctx);
   if (index < MAX_VERTEX_GENERIC_ATTRIBS)
      ATTRF(VBO_ATTRIB_GENERIC0 + index, 4, x, y, z, w);
   else
      ERROR(GL_INVALID_VALUE);
}

void GLAPIENTRY
_es_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
   VertexAttrib4f_nopos(index, x, y, z, w);
}


void GLAPIENTRY
_es_VertexAttrib1f(GLuint indx, GLfloat x)
{
   VertexAttrib4f_nopos(indx, x, 0.0f, 0.0f, 1.0f);
}


void GLAPIENTRY
_es_VertexAttrib1fv(GLuint indx, const GLfloat* values)
{
   VertexAttrib4f_nopos(indx, values[0], 0.0f, 0.0f, 1.0f);
}


void GLAPIENTRY
_es_VertexAttrib2f(GLuint indx, GLfloat x, GLfloat y)
{
   VertexAttrib4f_nopos(indx, x, y, 0.0f, 1.0f);
}


void GLAPIENTRY
_es_VertexAttrib2fv(GLuint indx, const GLfloat* values)
{
   VertexAttrib4f_nopos(indx, values[0], values[1], 0.0f, 1.0f);
}


void GLAPIENTRY
_es_VertexAttrib3f(GLuint indx, GLfloat x, GLfloat y, GLfloat z)
{
   VertexAttrib4f_nopos(indx, x, y, z, 1.0f);
}


void GLAPIENTRY
_es_VertexAttrib3fv(GLuint indx, const GLfloat* values)
{
   VertexAttrib4f_nopos(indx, values[0], values[1], values[2], 1.0f);
}


void GLAPIENTRY
_es_VertexAttrib4fv(GLuint indx, const GLfloat* values)
{
   VertexAttrib4f_nopos(indx, values[0], values[1], values[2], values[3]);
}
