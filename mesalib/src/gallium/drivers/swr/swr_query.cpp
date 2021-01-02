/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "util/os_time.h"
#include "swr_context.h"
#include "swr_fence.h"
#include "swr_query.h"
#include "swr_screen.h"
#include "swr_state.h"
#include "common/os.h"

static struct swr_query *
swr_query(struct pipe_query *p)
{
   return (struct swr_query *)p;
}

static struct pipe_query *
swr_create_query(struct pipe_context *pipe, unsigned type, unsigned index)
{
   struct swr_query *pq;

   assert(type < PIPE_QUERY_TYPES);
   assert(index < MAX_SO_STREAMS);

   pq = (struct swr_query *) AlignedMalloc(sizeof(struct swr_query), 64);

   if (pq) {
      memset(pq, 0, sizeof(*pq));
      pq->type = type;
      pq->index = index;
   }

   return (struct pipe_query *)pq;
}


static void
swr_destroy_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct swr_query *pq = swr_query(q);

   if (pq->fence) {
      if (swr_is_fence_pending(pq->fence))
         swr_fence_finish(pipe->screen, NULL, pq->fence, 0);
      swr_fence_reference(pipe->screen, &pq->fence, NULL);
   }

   AlignedFree(pq);
}


static bool
swr_get_query_result(struct pipe_context *pipe,
                     struct pipe_query *q,
                     bool wait,
                     union pipe_query_result *result)
{
   struct swr_query *pq = swr_query(q);
   unsigned index = pq->index;

   if (pq->fence) {
      if (!wait && !swr_is_fence_done(pq->fence))
         return false;

      swr_fence_finish(pipe->screen, NULL, pq->fence, 0);
      swr_fence_reference(pipe->screen, &pq->fence, NULL);
   }

   /* All values are reset to 0 at swr_begin_query, except starting timestamp.
    * Counters become simply end values.  */
   switch (pq->type) {
   /* Booleans */
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      result->b = pq->result.core.DepthPassCount != 0;
      break;
   case PIPE_QUERY_GPU_FINISHED:
      result->b = true;
      break;
   /* Counters */
   case PIPE_QUERY_OCCLUSION_COUNTER:
      result->u64 = pq->result.core.DepthPassCount;
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      result->u64 = pq->result.timestamp_end - pq->result.timestamp_start;
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      result->u64 = pq->result.coreFE.IaPrimitives;
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      result->u64 = pq->result.coreFE.SoNumPrimsWritten[index];
      break;
   /* Structures */
   case PIPE_QUERY_SO_STATISTICS: {
      struct pipe_query_data_so_statistics *so_stats = &result->so_statistics;
      so_stats->num_primitives_written =
         pq->result.coreFE.SoNumPrimsWritten[index];
      so_stats->primitives_storage_needed =
         pq->result.coreFE.SoPrimStorageNeeded[index];
   } break;
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
      /* os_get_time_nano returns nanoseconds */
      result->timestamp_disjoint.frequency = UINT64_C(1000000000);
      result->timestamp_disjoint.disjoint = FALSE;
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS: {
      struct pipe_query_data_pipeline_statistics *p_stats =
         &result->pipeline_statistics;
      p_stats->ia_vertices = pq->result.coreFE.IaVertices;
      p_stats->ia_primitives = pq->result.coreFE.IaPrimitives;
      p_stats->vs_invocations = pq->result.coreFE.VsInvocations;
      p_stats->gs_invocations = pq->result.coreFE.GsInvocations;
      p_stats->gs_primitives = pq->result.coreFE.GsPrimitives;
      p_stats->c_invocations = pq->result.coreFE.CPrimitives;
      p_stats->c_primitives = pq->result.coreFE.CPrimitives;
      p_stats->ps_invocations = pq->result.core.PsInvocations;
      p_stats->hs_invocations = pq->result.coreFE.HsInvocations;
      p_stats->ds_invocations = pq->result.coreFE.DsInvocations;
      p_stats->cs_invocations = pq->result.core.CsInvocations;
    } break;
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE: {
      uint64_t num_primitives_written =
         pq->result.coreFE.SoNumPrimsWritten[index];
      uint64_t primitives_storage_needed =
         pq->result.coreFE.SoPrimStorageNeeded[index];
      result->b = num_primitives_written > primitives_storage_needed;
   }
      break;
   default:
      assert(0 && "Unsupported query");
      break;
   }

   return true;
}

static bool
swr_begin_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_query *pq = swr_query(q);

   /* Initialize Results */
   memset(&pq->result, 0, sizeof(pq->result));
   switch (pq->type) {
   case PIPE_QUERY_GPU_FINISHED:
   case PIPE_QUERY_TIMESTAMP:
      /* nothing to do, but don't want the default */
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      pq->result.timestamp_start = swr_get_timestamp(pipe->screen);
      break;
   default:
      /* Core counters required.  Update draw context with location to
       * store results. */
      swr_update_draw_context(ctx, &pq->result);

      /* Only change stat collection if there are no active queries */
      if (ctx->active_queries == 0) {
         ctx->api.pfnSwrEnableStatsFE(ctx->swrContext, TRUE);
         ctx->api.pfnSwrEnableStatsBE(ctx->swrContext, TRUE);
      }
      ctx->active_queries++;
      break;
   }


   return true;
}

static bool
swr_end_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_query *pq = swr_query(q);

   switch (pq->type) {
   case PIPE_QUERY_GPU_FINISHED:
      /* nothing to do, but don't want the default */
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      pq->result.timestamp_end = swr_get_timestamp(pipe->screen);
      break;
   default:
      /* Stats are updated asynchronously, a fence is used to signal
       * completion. */
      if (!pq->fence) {
         struct swr_screen *screen = swr_screen(pipe->screen);
         swr_fence_reference(pipe->screen, &pq->fence, screen->flush_fence);
      }
      swr_fence_submit(ctx, pq->fence);

      /* Only change stat collection if there are no active queries */
      ctx->active_queries--;
      if (ctx->active_queries == 0) {
         ctx->api.pfnSwrEnableStatsFE(ctx->swrContext, FALSE);
         ctx->api.pfnSwrEnableStatsBE(ctx->swrContext, FALSE);
      }

      break;
   }

   return true;
}


bool
swr_check_render_cond(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);
   bool b, wait;
   uint64_t result;

   if (!ctx->render_cond_query)
      return true; /* no query predicate, draw normally */

   wait = (ctx->render_cond_mode == PIPE_RENDER_COND_WAIT
           || ctx->render_cond_mode == PIPE_RENDER_COND_BY_REGION_WAIT);

   b = pipe->get_query_result(
      pipe, ctx->render_cond_query, wait, (union pipe_query_result *)&result);
   if (b)
      return ((!result) == ctx->render_cond_cond);
   else
      return true;
}


static void
swr_set_active_query_state(struct pipe_context *pipe, bool enable)
{
}

void
swr_query_init(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);

   pipe->create_query = swr_create_query;
   pipe->destroy_query = swr_destroy_query;
   pipe->begin_query = swr_begin_query;
   pipe->end_query = swr_end_query;
   pipe->get_query_result = swr_get_query_result;
   pipe->set_active_query_state = swr_set_active_query_state;

   ctx->active_queries = 0;
}
