/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
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


/**
 * glBegin/EndQuery interface to pipe
 *
 * \author Brian Paul
 */


#include "main/imports.h"
#include "main/context.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "st_context.h"
#include "st_cb_queryobj.h"
#include "st_cb_bitmap.h"
#include "st_cb_bufferobjects.h"


static struct gl_query_object *
st_NewQueryObject(struct gl_context *ctx, GLuint id)
{
   struct st_query_object *stq = ST_CALLOC_STRUCT(st_query_object);
   if (stq) {
      stq->base.Id = id;
      stq->base.Ready = GL_TRUE;
      stq->pq = NULL;
      stq->type = PIPE_QUERY_TYPES; /* an invalid value */
      return &stq->base;
   }
   return NULL;
}



static void
st_DeleteQuery(struct gl_context *ctx, struct gl_query_object *q)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct st_query_object *stq = st_query_object(q);

   if (stq->pq) {
      pipe->destroy_query(pipe, stq->pq);
      stq->pq = NULL;
   }

   if (stq->pq_begin) {
      pipe->destroy_query(pipe, stq->pq_begin);
      stq->pq_begin = NULL;
   }

   free(stq);
}


static void
st_BeginQuery(struct gl_context *ctx, struct gl_query_object *q)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct st_query_object *stq = st_query_object(q);
   unsigned type;

   st_flush_bitmap_cache(st_context(ctx));

   /* convert GL query type to Gallium query type */
   switch (q->Target) {
   case GL_ANY_SAMPLES_PASSED:
   case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
      type = PIPE_QUERY_OCCLUSION_PREDICATE;
      break;
   case GL_SAMPLES_PASSED_ARB:
      type = PIPE_QUERY_OCCLUSION_COUNTER;
      break;
   case GL_PRIMITIVES_GENERATED:
      type = PIPE_QUERY_PRIMITIVES_GENERATED;
      break;
   case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
      type = PIPE_QUERY_PRIMITIVES_EMITTED;
      break;
   case GL_TIME_ELAPSED:
      if (st->has_time_elapsed)
         type = PIPE_QUERY_TIME_ELAPSED;
      else
         type = PIPE_QUERY_TIMESTAMP;
      break;
   case GL_VERTICES_SUBMITTED_ARB:
   case GL_PRIMITIVES_SUBMITTED_ARB:
   case GL_VERTEX_SHADER_INVOCATIONS_ARB:
   case GL_TESS_CONTROL_SHADER_PATCHES_ARB:
   case GL_TESS_EVALUATION_SHADER_INVOCATIONS_ARB:
   case GL_GEOMETRY_SHADER_INVOCATIONS:
   case GL_GEOMETRY_SHADER_PRIMITIVES_EMITTED_ARB:
   case GL_FRAGMENT_SHADER_INVOCATIONS_ARB:
   case GL_COMPUTE_SHADER_INVOCATIONS_ARB:
   case GL_CLIPPING_INPUT_PRIMITIVES_ARB:
   case GL_CLIPPING_OUTPUT_PRIMITIVES_ARB:
      type = PIPE_QUERY_PIPELINE_STATISTICS;
      break;
   default:
      assert(0 && "unexpected query target in st_BeginQuery()");
      return;
   }

   if (stq->type != type) {
      /* free old query of different type */
      if (stq->pq) {
         pipe->destroy_query(pipe, stq->pq);
         stq->pq = NULL;
      }
      if (stq->pq_begin) {
         pipe->destroy_query(pipe, stq->pq_begin);
         stq->pq_begin = NULL;
      }
      stq->type = PIPE_QUERY_TYPES; /* an invalid value */
   }

   if (q->Target == GL_TIME_ELAPSED &&
       type == PIPE_QUERY_TIMESTAMP) {
      /* Determine time elapsed by emitting two timestamp queries. */
      if (!stq->pq_begin) {
         stq->pq_begin = pipe->create_query(pipe, type, 0);
         stq->type = type;
      }
      pipe->end_query(pipe, stq->pq_begin);
   } else {
      if (!stq->pq) {
         stq->pq = pipe->create_query(pipe, type, q->Stream);
         stq->type = type;
      }
      if (stq->pq) {
         pipe->begin_query(pipe, stq->pq);
      }
      else {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glBeginQuery");
         return;
      }
   }
   assert(stq->type == type);
}


static void
st_EndQuery(struct gl_context *ctx, struct gl_query_object *q)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct st_query_object *stq = st_query_object(q);

   st_flush_bitmap_cache(st_context(ctx));

   if ((q->Target == GL_TIMESTAMP ||
        q->Target == GL_TIME_ELAPSED) &&
       !stq->pq) {
      stq->pq = pipe->create_query(pipe, PIPE_QUERY_TIMESTAMP, 0);
      stq->type = PIPE_QUERY_TIMESTAMP;
   }

   if (stq->pq)
      pipe->end_query(pipe, stq->pq);
}


static boolean
get_query_result(struct pipe_context *pipe,
                 struct st_query_object *stq,
                 boolean wait)
{
   union pipe_query_result data;

   if (!stq->pq) {
      /* Only needed in case we failed to allocate the gallium query earlier.
       * Return TRUE so we don't spin on this forever.
       */
      return TRUE;
   }

   if (!pipe->get_query_result(pipe, stq->pq, wait, &data))
      return FALSE;

   switch (stq->base.Target) {
   case GL_VERTICES_SUBMITTED_ARB:
      stq->base.Result = data.pipeline_statistics.ia_vertices;
      break;
   case GL_PRIMITIVES_SUBMITTED_ARB:
      stq->base.Result = data.pipeline_statistics.ia_primitives;
      break;
   case GL_VERTEX_SHADER_INVOCATIONS_ARB:
      stq->base.Result = data.pipeline_statistics.vs_invocations;
      break;
   case GL_TESS_CONTROL_SHADER_PATCHES_ARB:
      stq->base.Result = data.pipeline_statistics.hs_invocations;
      break;
   case GL_TESS_EVALUATION_SHADER_INVOCATIONS_ARB:
      stq->base.Result = data.pipeline_statistics.ds_invocations;
      break;
   case GL_GEOMETRY_SHADER_INVOCATIONS:
      stq->base.Result = data.pipeline_statistics.gs_invocations;
      break;
   case GL_GEOMETRY_SHADER_PRIMITIVES_EMITTED_ARB:
      stq->base.Result = data.pipeline_statistics.gs_primitives;
      break;
   case GL_FRAGMENT_SHADER_INVOCATIONS_ARB:
      stq->base.Result = data.pipeline_statistics.ps_invocations;
      break;
   case GL_COMPUTE_SHADER_INVOCATIONS_ARB:
      stq->base.Result = data.pipeline_statistics.cs_invocations;
      break;
   case GL_CLIPPING_INPUT_PRIMITIVES_ARB:
      stq->base.Result = data.pipeline_statistics.c_invocations;
      break;
   case GL_CLIPPING_OUTPUT_PRIMITIVES_ARB:
      stq->base.Result = data.pipeline_statistics.c_primitives;
      break;
   default:
      switch (stq->type) {
      case PIPE_QUERY_OCCLUSION_PREDICATE:
         stq->base.Result = !!data.b;
         break;
      default:
         stq->base.Result = data.u64;
         break;
      }
      break;
   }

   if (stq->base.Target == GL_TIME_ELAPSED &&
       stq->type == PIPE_QUERY_TIMESTAMP) {
      /* Calculate the elapsed time from the two timestamp queries */
      GLuint64EXT Result0 = 0;
      assert(stq->pq_begin);
      pipe->get_query_result(pipe, stq->pq_begin, TRUE, (void *)&Result0);
      stq->base.Result -= Result0;
   } else {
      assert(!stq->pq_begin);
   }

   return TRUE;
}


static void
st_WaitQuery(struct gl_context *ctx, struct gl_query_object *q)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct st_query_object *stq = st_query_object(q);

   /* this function should only be called if we don't have a ready result */
   assert(!stq->base.Ready);

   while (!stq->base.Ready &&
	  !get_query_result(pipe, stq, TRUE))
   {
      /* nothing */
   }

   q->Ready = GL_TRUE;
}


static void
st_CheckQuery(struct gl_context *ctx, struct gl_query_object *q)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct st_query_object *stq = st_query_object(q);
   assert(!q->Ready);   /* we should not get called if Ready is TRUE */
   q->Ready = get_query_result(pipe, stq, FALSE);
}


static uint64_t
st_GetTimestamp(struct gl_context *ctx)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct pipe_screen *screen = pipe->screen;

   /* Prefer the per-screen function */
   if (screen->get_timestamp) {
      return screen->get_timestamp(screen);
   }
   else {
      /* Fall back to the per-context function */
      assert(pipe->get_timestamp);
      return pipe->get_timestamp(pipe);
   }
}

static void
st_StoreQueryResult(struct gl_context *ctx, struct gl_query_object *q,
                    struct gl_buffer_object *buf, intptr_t offset,
                    GLenum pname, GLenum ptype)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct st_query_object *stq = st_query_object(q);
   struct st_buffer_object *stObj = st_buffer_object(buf);
   boolean wait = pname == GL_QUERY_RESULT;
   enum pipe_query_value_type result_type;
   int index;

   /* GL_QUERY_TARGET is a bit of an extension since it has nothing to
    * do with the GPU end of the query. Write it in "by hand".
    */
   if (pname == GL_QUERY_TARGET) {
      /* Assume that the data must be LE. The endianness situation wrt CPU and
       * GPU is incredibly confusing, but the vast majority of GPUs are
       * LE. When a BE one comes along, this needs some form of resolution.
       */
      unsigned data[2] = { CPU_TO_LE32(q->Target), 0 };
      pipe_buffer_write(pipe, stObj->buffer, offset,
                        (ptype == GL_INT64_ARB ||
                         ptype == GL_UNSIGNED_INT64_ARB) ? 8 : 4,
                        data);
      return;
   }

   switch (ptype) {
   case GL_INT:
      result_type = PIPE_QUERY_TYPE_I32;
      break;
   case GL_UNSIGNED_INT:
      result_type = PIPE_QUERY_TYPE_U32;
      break;
   case GL_INT64_ARB:
      result_type = PIPE_QUERY_TYPE_I64;
      break;
   case GL_UNSIGNED_INT64_ARB:
      result_type = PIPE_QUERY_TYPE_U64;
      break;
   default:
      unreachable("Unexpected result type");
   }

   if (pname == GL_QUERY_RESULT_AVAILABLE) {
      index = -1;
   } else if (stq->type == PIPE_QUERY_PIPELINE_STATISTICS) {
      switch (q->Target) {
      case GL_VERTICES_SUBMITTED_ARB:
         index = 0;
         break;
      case GL_PRIMITIVES_SUBMITTED_ARB:
         index = 1;
         break;
      case GL_VERTEX_SHADER_INVOCATIONS_ARB:
         index = 2;
         break;
      case GL_GEOMETRY_SHADER_INVOCATIONS:
         index = 3;
         break;
      case GL_GEOMETRY_SHADER_PRIMITIVES_EMITTED_ARB:
         index = 4;
         break;
      case GL_CLIPPING_INPUT_PRIMITIVES_ARB:
         index = 5;
         break;
      case GL_CLIPPING_OUTPUT_PRIMITIVES_ARB:
         index = 6;
         break;
      case GL_FRAGMENT_SHADER_INVOCATIONS_ARB:
         index = 7;
         break;
      case GL_TESS_CONTROL_SHADER_PATCHES_ARB:
         index = 8;
         break;
      case GL_TESS_EVALUATION_SHADER_INVOCATIONS_ARB:
         index = 9;
         break;
      case GL_COMPUTE_SHADER_INVOCATIONS_ARB:
         index = 10;
         break;
      default:
         unreachable("Unexpected target");
      }
   } else {
      index = 0;
   }

   pipe->get_query_result_resource(pipe, stq->pq, wait, result_type, index,
                                   stObj->buffer, offset);
}

void st_init_query_functions(struct dd_function_table *functions)
{
   functions->NewQueryObject = st_NewQueryObject;
   functions->DeleteQuery = st_DeleteQuery;
   functions->BeginQuery = st_BeginQuery;
   functions->EndQuery = st_EndQuery;
   functions->WaitQuery = st_WaitQuery;
   functions->CheckQuery = st_CheckQuery;
   functions->GetTimestamp = st_GetTimestamp;
   functions->StoreQueryResult = st_StoreQueryResult;
}
