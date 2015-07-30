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
 */


#include "glheader.h"
#include "context.h"
#include "enums.h"
#include "hash.h"
#include "imports.h"
#include "queryobj.h"
#include "mtypes.h"
#include "main/dispatch.h"


/**
 * Allocate a new query object.  This is a fallback routine called via
 * ctx->Driver.NewQueryObject().
 * \param ctx - rendering context
 * \param id - the new object's ID
 * \return pointer to new query_object object or NULL if out of memory.
 */
static struct gl_query_object *
_mesa_new_query_object(struct gl_context *ctx, GLuint id)
{
   struct gl_query_object *q = CALLOC_STRUCT(gl_query_object);
   (void) ctx;
   if (q) {
      q->Id = id;
      q->Result = 0;
      q->Active = GL_FALSE;

      /* This is to satisfy the language of the specification: "In the initial
       * state of a query object, the result is available" (OpenGL 3.1 §
       * 2.13).
       */
      q->Ready = GL_TRUE;

      /* OpenGL 3.1 § 2.13 says about GenQueries, "These names are marked as
       * used, but no object is associated with them until the first time they
       * are used by BeginQuery." Since our implementation actually does
       * allocate an object at this point, use a flag to indicate that this
       * object has not yet been bound so should not be considered a query.
       */
      q->EverBound = GL_FALSE;
   }
   return q;
}


/**
 * Begin a query.  Software driver fallback.
 * Called via ctx->Driver.BeginQuery().
 */
static void
_mesa_begin_query(struct gl_context *ctx, struct gl_query_object *q)
{
   ctx->NewState |= _NEW_DEPTH; /* for swrast */
}


/**
 * End a query.  Software driver fallback.
 * Called via ctx->Driver.EndQuery().
 */
static void
_mesa_end_query(struct gl_context *ctx, struct gl_query_object *q)
{
   ctx->NewState |= _NEW_DEPTH; /* for swrast */
   q->Ready = GL_TRUE;
}


/**
 * Wait for query to complete.  Software driver fallback.
 * Called via ctx->Driver.WaitQuery().
 */
static void
_mesa_wait_query(struct gl_context *ctx, struct gl_query_object *q)
{
   /* For software drivers, _mesa_end_query() should have completed the query.
    * For real hardware, implement a proper WaitQuery() driver function,
    * which may require issuing a flush.
    */
   assert(q->Ready);
}


/**
 * Check if a query results are ready.  Software driver fallback.
 * Called via ctx->Driver.CheckQuery().
 */
static void
_mesa_check_query(struct gl_context *ctx, struct gl_query_object *q)
{
   /* No-op for sw rendering.
    * HW drivers may need to flush at this time.
    */
}


/**
 * Delete a query object.  Called via ctx->Driver.DeleteQuery().
 * Not removed from hash table here.
 */
static void
_mesa_delete_query(struct gl_context *ctx, struct gl_query_object *q)
{
   free(q->Label);
   free(q);
}


void
_mesa_init_query_object_functions(struct dd_function_table *driver)
{
   driver->NewQueryObject = _mesa_new_query_object;
   driver->DeleteQuery = _mesa_delete_query;
   driver->BeginQuery = _mesa_begin_query;
   driver->EndQuery = _mesa_end_query;
   driver->WaitQuery = _mesa_wait_query;
   driver->CheckQuery = _mesa_check_query;
}

static struct gl_query_object **
get_pipe_stats_binding_point(struct gl_context *ctx,
                             GLenum target)
{
   const int which = target - GL_VERTICES_SUBMITTED_ARB;
   assert(which < MAX_PIPELINE_STATISTICS);

   if (!_mesa_is_desktop_gl(ctx) ||
       !ctx->Extensions.ARB_pipeline_statistics_query)
      return NULL;

   return &ctx->Query.pipeline_stats[which];
}

/**
 * Return pointer to the query object binding point for the given target and
 * index.
 * \return NULL if invalid target, else the address of binding point
 */
static struct gl_query_object **
get_query_binding_point(struct gl_context *ctx, GLenum target, GLuint index)
{
   switch (target) {
   case GL_SAMPLES_PASSED_ARB:
      if (ctx->Extensions.ARB_occlusion_query)
         return &ctx->Query.CurrentOcclusionObject;
      else
         return NULL;
   case GL_ANY_SAMPLES_PASSED:
      if (ctx->Extensions.ARB_occlusion_query2)
         return &ctx->Query.CurrentOcclusionObject;
      else
         return NULL;
   case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
      if (ctx->Extensions.ARB_ES3_compatibility
          || (ctx->API == API_OPENGLES2 && ctx->Version >= 30))
         return &ctx->Query.CurrentOcclusionObject;
      else
         return NULL;
   case GL_TIME_ELAPSED_EXT:
      if (ctx->Extensions.EXT_timer_query)
         return &ctx->Query.CurrentTimerObject;
      else
         return NULL;
   case GL_PRIMITIVES_GENERATED:
      if (ctx->Extensions.EXT_transform_feedback)
         return &ctx->Query.PrimitivesGenerated[index];
      else
         return NULL;
   case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
      if (ctx->Extensions.EXT_transform_feedback)
         return &ctx->Query.PrimitivesWritten[index];
      else
         return NULL;

   case GL_VERTICES_SUBMITTED_ARB:
   case GL_PRIMITIVES_SUBMITTED_ARB:
   case GL_VERTEX_SHADER_INVOCATIONS_ARB:
   case GL_FRAGMENT_SHADER_INVOCATIONS_ARB:
   case GL_CLIPPING_INPUT_PRIMITIVES_ARB:
   case GL_CLIPPING_OUTPUT_PRIMITIVES_ARB:
         return get_pipe_stats_binding_point(ctx, target);

   case GL_GEOMETRY_SHADER_INVOCATIONS:
      /* GL_GEOMETRY_SHADER_INVOCATIONS is defined in a non-sequential order */
      target = GL_VERTICES_SUBMITTED_ARB + MAX_PIPELINE_STATISTICS - 1;
      /* fallthrough */
   case GL_GEOMETRY_SHADER_PRIMITIVES_EMITTED_ARB:
      if (_mesa_has_geometry_shaders(ctx))
         return get_pipe_stats_binding_point(ctx, target);
      else
         return NULL;

   case GL_TESS_CONTROL_SHADER_PATCHES_ARB:
   case GL_TESS_EVALUATION_SHADER_INVOCATIONS_ARB:
      if (_mesa_has_tessellation(ctx))
         return get_pipe_stats_binding_point(ctx, target);
      else
         return NULL;

   case GL_COMPUTE_SHADER_INVOCATIONS_ARB:
      if (_mesa_has_compute_shaders(ctx))
         return get_pipe_stats_binding_point(ctx, target);
      else
         return NULL;

   default:
      return NULL;
   }
}

/**
 * Create $n query objects and store them in *ids. Make them of type $target
 * if dsa is set. Called from _mesa_GenQueries() and _mesa_CreateQueries().
 */
static void
create_queries(struct gl_context *ctx, GLenum target, GLsizei n, GLuint *ids,
               bool dsa)
{
   const char *func = dsa ? "glGenQueries" : "glCreateQueries";
   GLuint first;

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "%s(%d)\n", func, n);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   first = _mesa_HashFindFreeKeyBlock(ctx->Query.QueryObjects, n);
   if (first) {
      GLsizei i;
      for (i = 0; i < n; i++) {
         struct gl_query_object *q
            = ctx->Driver.NewQueryObject(ctx, first + i);
         if (!q) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
            return;
         } else if (dsa) {
            /* Do the equivalent of binding the buffer with a target */
            q->Target = target;
            q->EverBound = GL_TRUE;
         }
         ids[i] = first + i;
         _mesa_HashInsert(ctx->Query.QueryObjects, first + i, q);
      }
   }
}

void GLAPIENTRY
_mesa_GenQueries(GLsizei n, GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   create_queries(ctx, 0, n, ids, false);
}

void GLAPIENTRY
_mesa_CreateQueries(GLenum target, GLsizei n, GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);

   switch (target) {
   case GL_SAMPLES_PASSED:
   case GL_ANY_SAMPLES_PASSED:
   case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
   case GL_TIME_ELAPSED:
   case GL_TIMESTAMP:
   case GL_PRIMITIVES_GENERATED:
   case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glCreateQueries(invalid target = %s)",
                  _mesa_enum_to_string(target));
      return;
   }

   create_queries(ctx, target, n, ids, true);
}


void GLAPIENTRY
_mesa_DeleteQueries(GLsizei n, const GLuint *ids)
{
   GLint i;
   GET_CURRENT_CONTEXT(ctx);
   FLUSH_VERTICES(ctx, 0);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glDeleteQueries(%d)\n", n);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteQueriesARB(n < 0)");
      return;
   }

   for (i = 0; i < n; i++) {
      if (ids[i] > 0) {
         struct gl_query_object *q = _mesa_lookup_query_object(ctx, ids[i]);
         if (q) {
            if (q->Active) {
               struct gl_query_object **bindpt;
               bindpt = get_query_binding_point(ctx, q->Target, q->Stream);
               assert(bindpt); /* Should be non-null for active q. */
               if (bindpt) {
                  *bindpt = NULL;
               }
               q->Active = GL_FALSE;
               ctx->Driver.EndQuery(ctx, q);
            }
            _mesa_HashRemove(ctx->Query.QueryObjects, ids[i]);
            ctx->Driver.DeleteQuery(ctx, q);
         }
      }
   }
}


GLboolean GLAPIENTRY
_mesa_IsQuery(GLuint id)
{
   struct gl_query_object *q;

   GET_CURRENT_CONTEXT(ctx);
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glIsQuery(%u)\n", id);

   if (id == 0)
      return GL_FALSE;

   q = _mesa_lookup_query_object(ctx, id);
   if (q == NULL)
      return GL_FALSE;

   return q->EverBound;
}

static GLboolean
query_error_check_index(struct gl_context *ctx, GLenum target, GLuint index)
{
   switch (target) {
   case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
   case GL_PRIMITIVES_GENERATED:
      if (index >= ctx->Const.MaxVertexStreams) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glBeginQueryIndexed(index>=MaxVertexStreams)");
         return GL_FALSE;
      }
      break;
   default:
      if (index > 0) {
         _mesa_error(ctx, GL_INVALID_VALUE, "glBeginQueryIndexed(index>0)");
         return GL_FALSE;
      }
   }
   return GL_TRUE;
}

void GLAPIENTRY
_mesa_BeginQueryIndexed(GLenum target, GLuint index, GLuint id)
{
   struct gl_query_object *q, **bindpt;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glBeginQueryIndexed(%s, %u, %u)\n",
                  _mesa_enum_to_string(target), index, id);

   if (!query_error_check_index(ctx, target, index))
      return;

   FLUSH_VERTICES(ctx, 0);

   bindpt = get_query_binding_point(ctx, target, index);
   if (!bindpt) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glBeginQuery{Indexed}(target)");
      return;
   }

   /* From the GL_ARB_occlusion_query spec:
    *
    *     "If BeginQueryARB is called while another query is already in
    *      progress with the same target, an INVALID_OPERATION error is
    *      generated."
    */
   if (*bindpt) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBeginQuery{Indexed}(target=%s is active)",
                  _mesa_enum_to_string(target));
      return;
   }

   if (id == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glBeginQuery{Indexed}(id==0)");
      return;
   }

   q = _mesa_lookup_query_object(ctx, id);
   if (!q) {
      if (ctx->API != API_OPENGL_COMPAT) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glBeginQuery{Indexed}(non-gen name)");
         return;
      } else {
         /* create new object */
         q = ctx->Driver.NewQueryObject(ctx, id);
         if (!q) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "glBeginQuery{Indexed}");
            return;
         }
         _mesa_HashInsert(ctx->Query.QueryObjects, id, q);
      }
   }
   else {
      /* pre-existing object */
      if (q->Active) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glBeginQuery{Indexed}(query already active)");
         return;
      }

      /* Section 2.14 Asynchronous Queries, page 84 of the OpenGL ES 3.0.4
       * spec states:
       *
       *     "BeginQuery generates an INVALID_OPERATION error if any of the
       *      following conditions hold: [...] id is the name of an
       *      existing query object whose type does not match target; [...]
       *
       * Similar wording exists in the OpenGL 4.5 spec, section 4.2. QUERY
       * OBJECTS AND ASYNCHRONOUS QUERIES, page 43.
       */
      if (q->EverBound && q->Target != target) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glBeginQuery{Indexed}(target mismatch)");
         return;
      }
   }

   /* This possibly changes the target of a buffer allocated by
    * CreateQueries. Issue 39) in the ARB_direct_state_access extension states
    * the following:
    *
    * "CreateQueries adds a <target>, so strictly speaking the <target>
    * command isn't needed for BeginQuery/EndQuery, but in the end, this also
    * isn't a selector, so we decided not to change it."
    *
    * Updating the target of the query object should be acceptable, so let's
    * do that.
    */

   q->Target = target;
   q->Active = GL_TRUE;
   q->Result = 0;
   q->Ready = GL_FALSE;
   q->EverBound = GL_TRUE;
   q->Stream = index;

   /* XXX should probably refcount query objects */
   *bindpt = q;

   ctx->Driver.BeginQuery(ctx, q);
}


void GLAPIENTRY
_mesa_EndQueryIndexed(GLenum target, GLuint index)
{
   struct gl_query_object *q, **bindpt;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glEndQueryIndexed(%s, %u)\n",
                  _mesa_enum_to_string(target), index);

   if (!query_error_check_index(ctx, target, index))
      return;

   FLUSH_VERTICES(ctx, 0);

   bindpt = get_query_binding_point(ctx, target, index);
   if (!bindpt) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glEndQuery{Indexed}(target)");
      return;
   }

   /* XXX should probably refcount query objects */
   q = *bindpt;

   /* Check for GL_ANY_SAMPLES_PASSED vs GL_SAMPLES_PASSED. */
   if (q && q->Target != target) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glEndQuery(target=%s with active query of target %s)",
                  _mesa_enum_to_string(target),
                  _mesa_enum_to_string(q->Target));
      return;
   }

   *bindpt = NULL;

   if (!q || !q->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glEndQuery{Indexed}(no matching glBeginQuery{Indexed})");
      return;
   }

   q->Active = GL_FALSE;
   ctx->Driver.EndQuery(ctx, q);
}

void GLAPIENTRY
_mesa_BeginQuery(GLenum target, GLuint id)
{
   _mesa_BeginQueryIndexed(target, 0, id);
}

void GLAPIENTRY
_mesa_EndQuery(GLenum target)
{
   _mesa_EndQueryIndexed(target, 0);
}

void GLAPIENTRY
_mesa_QueryCounter(GLuint id, GLenum target)
{
   struct gl_query_object *q;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glQueryCounter(%u, %s)\n", id,
                  _mesa_enum_to_string(target));

   /* error checking */
   if (target != GL_TIMESTAMP) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glQueryCounter(target)");
      return;
   }

   if (id == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glQueryCounter(id==0)");
      return;
   }

   q = _mesa_lookup_query_object(ctx, id);
   if (!q) {
      /* XXX the Core profile should throw INVALID_OPERATION here */

      /* create new object */
      q = ctx->Driver.NewQueryObject(ctx, id);
      if (!q) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glQueryCounter");
         return;
      }
      _mesa_HashInsert(ctx->Query.QueryObjects, id, q);
   }
   else {
      if (q->Target && q->Target != GL_TIMESTAMP) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glQueryCounter(id has an invalid target)");
         return;
      }
   }

   if (q->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glQueryCounter(id is active)");
      return;
   }

   /* This possibly changes the target of a buffer allocated by
    * CreateQueries. Issue 39) in the ARB_direct_state_access extension states
    * the following:
    *
    * "CreateQueries adds a <target>, so strictly speaking the <target>
    * command isn't needed for BeginQuery/EndQuery, but in the end, this also
    * isn't a selector, so we decided not to change it."
    *
    * Updating the target of the query object should be acceptable, so let's
    * do that.
    */

   q->Target = target;
   q->Result = 0;
   q->Ready = GL_FALSE;
   q->EverBound = GL_TRUE;

   if (ctx->Driver.QueryCounter) {
      ctx->Driver.QueryCounter(ctx, q);
   } else {
      /* QueryCounter is implemented using EndQuery without BeginQuery
       * in drivers. This is actually Direct3D and Gallium convention.
       */
      ctx->Driver.EndQuery(ctx, q);
   }
}


void GLAPIENTRY
_mesa_GetQueryIndexediv(GLenum target, GLuint index, GLenum pname,
                        GLint *params)
{
   struct gl_query_object *q = NULL, **bindpt = NULL;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glGetQueryIndexediv(%s, %u, %s)\n",
                  _mesa_enum_to_string(target),
                  index,
                  _mesa_enum_to_string(pname));

   if (!query_error_check_index(ctx, target, index))
      return;

   if (target == GL_TIMESTAMP) {
      if (!ctx->Extensions.ARB_timer_query) {
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQueryARB(target)");
         return;
      }
   }
   else {
      bindpt = get_query_binding_point(ctx, target, index);
      if (!bindpt) {
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQuery{Indexed}iv(target)");
         return;
      }

      q = *bindpt;
   }

   switch (pname) {
      case GL_QUERY_COUNTER_BITS_ARB:
         switch (target) {
         case GL_SAMPLES_PASSED:
            *params = ctx->Const.QueryCounterBits.SamplesPassed;
            break;
         case GL_ANY_SAMPLES_PASSED:
            /* The minimum value of this is 1 if it's nonzero, and the value
             * is only ever GL_TRUE or GL_FALSE, so no sense in reporting more
             * bits.
             */
            *params = 1;
            break;
         case GL_TIME_ELAPSED:
            *params = ctx->Const.QueryCounterBits.TimeElapsed;
            break;
         case GL_TIMESTAMP:
            *params = ctx->Const.QueryCounterBits.Timestamp;
            break;
         case GL_PRIMITIVES_GENERATED:
            *params = ctx->Const.QueryCounterBits.PrimitivesGenerated;
            break;
         case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
            *params = ctx->Const.QueryCounterBits.PrimitivesWritten;
            break;
         case GL_VERTICES_SUBMITTED_ARB:
            *params = ctx->Const.QueryCounterBits.VerticesSubmitted;
            break;
         case GL_PRIMITIVES_SUBMITTED_ARB:
            *params = ctx->Const.QueryCounterBits.PrimitivesSubmitted;
            break;
         case GL_VERTEX_SHADER_INVOCATIONS_ARB:
            *params = ctx->Const.QueryCounterBits.VsInvocations;
            break;
         case GL_TESS_CONTROL_SHADER_PATCHES_ARB:
            *params = ctx->Const.QueryCounterBits.TessPatches;
            break;
         case GL_TESS_EVALUATION_SHADER_INVOCATIONS_ARB:
            *params = ctx->Const.QueryCounterBits.TessInvocations;
            break;
         case GL_GEOMETRY_SHADER_INVOCATIONS:
            *params = ctx->Const.QueryCounterBits.GsInvocations;
            break;
         case GL_GEOMETRY_SHADER_PRIMITIVES_EMITTED_ARB:
            *params = ctx->Const.QueryCounterBits.GsPrimitives;
            break;
         case GL_FRAGMENT_SHADER_INVOCATIONS_ARB:
            *params = ctx->Const.QueryCounterBits.FsInvocations;
            break;
         case GL_COMPUTE_SHADER_INVOCATIONS_ARB:
            *params = ctx->Const.QueryCounterBits.ComputeInvocations;
            break;
         case GL_CLIPPING_INPUT_PRIMITIVES_ARB:
            *params = ctx->Const.QueryCounterBits.ClInPrimitives;
            break;
         case GL_CLIPPING_OUTPUT_PRIMITIVES_ARB:
            *params = ctx->Const.QueryCounterBits.ClOutPrimitives;
            break;
         default:
            _mesa_problem(ctx,
                          "Unknown target in glGetQueryIndexediv(target = %s)",
                          _mesa_enum_to_string(target));
            *params = 0;
            break;
         }
         break;
      case GL_CURRENT_QUERY_ARB:
         *params = (q && q->Target == target) ? q->Id : 0;
         break;
      default:
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQuery{Indexed}iv(pname)");
         return;
   }
}

void GLAPIENTRY
_mesa_GetQueryiv(GLenum target, GLenum pname, GLint *params)
{
   _mesa_GetQueryIndexediv(target, 0, pname, params);
}

void GLAPIENTRY
_mesa_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params)
{
   struct gl_query_object *q = NULL;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glGetQueryObjectiv(%u, %s)\n", id,
                  _mesa_enum_to_string(pname));

   if (id)
      q = _mesa_lookup_query_object(ctx, id);

   if (!q || q->Active || !q->EverBound) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetQueryObjectivARB(id=%d is invalid or active)", id);
      return;
   }

   switch (pname) {
      case GL_QUERY_RESULT_ARB:
         if (!q->Ready)
            ctx->Driver.WaitQuery(ctx, q);
         /* if result is too large for returned type, clamp to max value */
         if (q->Target == GL_ANY_SAMPLES_PASSED
             || q->Target == GL_ANY_SAMPLES_PASSED_CONSERVATIVE) {
            if (q->Result)
               *params = GL_TRUE;
            else
               *params = GL_FALSE;
         } else {
            if (q->Result > 0x7fffffff) {
               *params = 0x7fffffff;
            }
            else {
               *params = (GLint)q->Result;
            }
         }
         break;
      case GL_QUERY_RESULT_AVAILABLE_ARB:
         if (!q->Ready)
            ctx->Driver.CheckQuery( ctx, q );
         *params = q->Ready;
         break;
      case GL_QUERY_TARGET:
         *params = q->Target;
         break;
      default:
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQueryObjectivARB(pname)");
         return;
   }
}


void GLAPIENTRY
_mesa_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params)
{
   struct gl_query_object *q = NULL;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glGetQueryObjectuiv(%u, %s)\n", id,
                  _mesa_enum_to_string(pname));

   if (id)
      q = _mesa_lookup_query_object(ctx, id);

   if (!q || q->Active || !q->EverBound) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetQueryObjectuivARB(id=%d is invalid or active)", id);
      return;
   }

   switch (pname) {
      case GL_QUERY_RESULT_ARB:
         if (!q->Ready)
            ctx->Driver.WaitQuery(ctx, q);
         /* if result is too large for returned type, clamp to max value */
         if (q->Target == GL_ANY_SAMPLES_PASSED
             || q->Target == GL_ANY_SAMPLES_PASSED_CONSERVATIVE) {
            if (q->Result)
               *params = GL_TRUE;
            else
               *params = GL_FALSE;
         } else {
            if (q->Result > 0xffffffff) {
               *params = 0xffffffff;
            }
            else {
               *params = (GLuint)q->Result;
            }
         }
         break;
      case GL_QUERY_RESULT_AVAILABLE_ARB:
         if (!q->Ready)
            ctx->Driver.CheckQuery( ctx, q );
         *params = q->Ready;
         break;
      case GL_QUERY_TARGET:
         *params = q->Target;
         break;
      default:
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQueryObjectuivARB(pname)");
         return;
   }
}


/**
 * New with GL_EXT_timer_query
 */
void GLAPIENTRY
_mesa_GetQueryObjecti64v(GLuint id, GLenum pname, GLint64EXT *params)
{
   struct gl_query_object *q = NULL;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glGetQueryObjecti64v(%u, %s)\n", id,
                  _mesa_enum_to_string(pname));

   if (id)
      q = _mesa_lookup_query_object(ctx, id);

   if (!q || q->Active || !q->EverBound) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetQueryObjectui64vARB(id=%d is invalid or active)", id);
      return;
   }

   switch (pname) {
      case GL_QUERY_RESULT_ARB:
         if (!q->Ready)
            ctx->Driver.WaitQuery(ctx, q);
         *params = q->Result;
         break;
      case GL_QUERY_RESULT_AVAILABLE_ARB:
         if (!q->Ready)
            ctx->Driver.CheckQuery( ctx, q );
         *params = q->Ready;
         break;
      case GL_QUERY_TARGET:
         *params = q->Target;
         break;
      default:
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQueryObjecti64vARB(pname)");
         return;
   }
}


/**
 * New with GL_EXT_timer_query
 */
void GLAPIENTRY
_mesa_GetQueryObjectui64v(GLuint id, GLenum pname, GLuint64EXT *params)
{
   struct gl_query_object *q = NULL;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glGetQueryObjectui64v(%u, %s)\n", id,
                  _mesa_enum_to_string(pname));

   if (id)
      q = _mesa_lookup_query_object(ctx, id);

   if (!q || q->Active || !q->EverBound) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetQueryObjectuui64vARB(id=%d is invalid or active)", id);
      return;
   }

   switch (pname) {
      case GL_QUERY_RESULT_ARB:
         if (!q->Ready)
            ctx->Driver.WaitQuery(ctx, q);
         *params = q->Result;
         break;
      case GL_QUERY_RESULT_AVAILABLE_ARB:
         if (!q->Ready)
            ctx->Driver.CheckQuery( ctx, q );
         *params = q->Ready;
         break;
      case GL_QUERY_TARGET:
         *params = q->Target;
         break;
      default:
         _mesa_error(ctx, GL_INVALID_ENUM, "glGetQueryObjectui64vARB(pname)");
         return;
   }
}

/**
 * New with GL_ARB_query_buffer_object
 */
void GLAPIENTRY
_mesa_GetQueryBufferObjectiv(GLuint id, GLuint buffer, GLenum pname,
                             GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_error(ctx, GL_INVALID_OPERATION, "glGetQueryBufferObjectiv");
}


void GLAPIENTRY
_mesa_GetQueryBufferObjectuiv(GLuint id, GLuint buffer, GLenum pname,
                              GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_error(ctx, GL_INVALID_OPERATION, "glGetQueryBufferObjectuiv");
}


void GLAPIENTRY
_mesa_GetQueryBufferObjecti64v(GLuint id, GLuint buffer, GLenum pname,
                               GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_error(ctx, GL_INVALID_OPERATION, "glGetQueryBufferObjecti64v");
}


void GLAPIENTRY
_mesa_GetQueryBufferObjectui64v(GLuint id, GLuint buffer, GLenum pname,
                                GLintptr offset)
{
   GET_CURRENT_CONTEXT(ctx);
   _mesa_error(ctx, GL_INVALID_OPERATION, "glGetQueryBufferObjectui64v");
}


/**
 * Allocate/init the context state related to query objects.
 */
void
_mesa_init_queryobj(struct gl_context *ctx)
{
   ctx->Query.QueryObjects = _mesa_NewHashTable();
   ctx->Query.CurrentOcclusionObject = NULL;

   ctx->Const.QueryCounterBits.SamplesPassed = 64;
   ctx->Const.QueryCounterBits.TimeElapsed = 64;
   ctx->Const.QueryCounterBits.Timestamp = 64;
   ctx->Const.QueryCounterBits.PrimitivesGenerated = 64;
   ctx->Const.QueryCounterBits.PrimitivesWritten = 64;

   ctx->Const.QueryCounterBits.VerticesSubmitted = 64;
   ctx->Const.QueryCounterBits.PrimitivesSubmitted = 64;
   ctx->Const.QueryCounterBits.VsInvocations = 64;
   ctx->Const.QueryCounterBits.TessPatches = 64;
   ctx->Const.QueryCounterBits.TessInvocations = 64;
   ctx->Const.QueryCounterBits.GsInvocations = 64;
   ctx->Const.QueryCounterBits.GsPrimitives = 64;
   ctx->Const.QueryCounterBits.FsInvocations = 64;
   ctx->Const.QueryCounterBits.ComputeInvocations = 64;
   ctx->Const.QueryCounterBits.ClInPrimitives = 64;
   ctx->Const.QueryCounterBits.ClOutPrimitives = 64;
}


/**
 * Callback for deleting a query object.  Called by _mesa_HashDeleteAll().
 */
static void
delete_queryobj_cb(GLuint id, void *data, void *userData)
{
   struct gl_query_object *q= (struct gl_query_object *) data;
   struct gl_context *ctx = (struct gl_context *)userData;
   ctx->Driver.DeleteQuery(ctx, q);
}


/**
 * Free the context state related to query objects.
 */
void
_mesa_free_queryobj_data(struct gl_context *ctx)
{
   _mesa_HashDeleteAll(ctx->Query.QueryObjects, delete_queryobj_cb, ctx);
   _mesa_DeleteHashTable(ctx->Query.QueryObjects);
}
