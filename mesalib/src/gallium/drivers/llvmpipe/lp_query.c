/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * Copyright 2010 VMware, Inc.
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
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

/* Authors:
 *    Keith Whitwell, Qicheng Christopher Li, Brian Paul
 */

#include "draw/draw_context.h"
#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "util/os_time.h"
#include "lp_context.h"
#include "lp_flush.h"
#include "lp_fence.h"
#include "lp_query.h"
#include "lp_screen.h"
#include "lp_state.h"
#include "lp_rast.h"


static struct llvmpipe_query *llvmpipe_query( struct pipe_query *p )
{
   return (struct llvmpipe_query *)p;
}

static struct pipe_query *
llvmpipe_create_query(struct pipe_context *pipe, 
                      unsigned type,
                      unsigned index)
{
   struct llvmpipe_query *pq;

   assert(type < PIPE_QUERY_TYPES);

   pq = CALLOC_STRUCT( llvmpipe_query );

   if (pq) {
      pq->type = type;
      pq->index = index;
   }

   return (struct pipe_query *) pq;
}


static void
llvmpipe_destroy_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct llvmpipe_query *pq = llvmpipe_query(q);

   /* Ideally we would refcount queries & not get destroyed until the
    * last scene had finished with us.
    */
   if (pq->fence) {
      if (!lp_fence_issued(pq->fence))
         llvmpipe_flush(pipe, NULL, __FUNCTION__);

      if (!lp_fence_signalled(pq->fence))
         lp_fence_wait(pq->fence);

      lp_fence_reference(&pq->fence, NULL);
   }

   FREE(pq);
}


static bool
llvmpipe_get_query_result(struct pipe_context *pipe, 
                          struct pipe_query *q,
                          bool wait,
                          union pipe_query_result *vresult)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pipe->screen);
   unsigned num_threads = MAX2(1, screen->num_threads);
   struct llvmpipe_query *pq = llvmpipe_query(q);
   uint64_t *result = (uint64_t *)vresult;
   int i;

   if (pq->fence) {
      /* only have a fence if there was a scene */
      if (!lp_fence_signalled(pq->fence)) {
         if (!lp_fence_issued(pq->fence))
            llvmpipe_flush(pipe, NULL, __FUNCTION__);

         if (!wait)
            return false;

         lp_fence_wait(pq->fence);
      }
   }

   /* Sum the results from each of the threads:
    */
   *result = 0;

   switch (pq->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      for (i = 0; i < num_threads; i++) {
         *result += pq->end[i];
      }
      break;
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      for (i = 0; i < num_threads; i++) {
         /* safer (still not guaranteed) when there's an overflow */
         vresult->b = vresult->b || pq->end[i];
      }
      break;
   case PIPE_QUERY_TIMESTAMP:
      for (i = 0; i < num_threads; i++) {
         if (pq->end[i] > *result) {
            *result = pq->end[i];
         }
      }
      break;
   case PIPE_QUERY_TIME_ELAPSED: {
      uint64_t start = (uint64_t)-1, end = 0;
      for (i = 0; i < num_threads; i++) {
         if (pq->start[i] && pq->start[i] < start)
            start = pq->start[i];
         if (pq->end[i] && pq->end[i] > end)
            end = pq->end[i];
      }
      *result = end - start;
      break;
   }
   case PIPE_QUERY_TIMESTAMP_DISJOINT: {
      struct pipe_query_data_timestamp_disjoint *td =
         (struct pipe_query_data_timestamp_disjoint *)vresult;
      /* os_get_time_nano return nanoseconds */
      td->frequency = UINT64_C(1000000000);
      td->disjoint = false;
   }
      break;
   case PIPE_QUERY_GPU_FINISHED:
      vresult->b = true;
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      *result = pq->num_primitives_generated[0];
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      *result = pq->num_primitives_written[0];
      break;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      vresult->b = false;
      for (unsigned s = 0; s < PIPE_MAX_VERTEX_STREAMS; s++)
         vresult->b |= pq->num_primitives_generated[s] > pq->num_primitives_written[s];
      break;
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      vresult->b = pq->num_primitives_generated[0] > pq->num_primitives_written[0];
      break;
   case PIPE_QUERY_SO_STATISTICS: {
      struct pipe_query_data_so_statistics *stats =
         (struct pipe_query_data_so_statistics *)vresult;
      stats->num_primitives_written = pq->num_primitives_written[0];
      stats->primitives_storage_needed = pq->num_primitives_generated[0];
   }
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS: {
      struct pipe_query_data_pipeline_statistics *stats =
         (struct pipe_query_data_pipeline_statistics *)vresult;
      /* only ps_invocations come from binned query */
      for (i = 0; i < num_threads; i++) {
         pq->stats.ps_invocations += pq->end[i];
      }
      pq->stats.ps_invocations *= LP_RASTER_BLOCK_SIZE * LP_RASTER_BLOCK_SIZE;
      *stats = pq->stats;
   }
      break;
   default:
      assert(0);
      break;
   }

   return true;
}

static void
llvmpipe_get_query_result_resource(struct pipe_context *pipe,
                                   struct pipe_query *q,
                                   bool wait,
                                   enum pipe_query_value_type result_type,
                                   int index,
                                   struct pipe_resource *resource,
                                   unsigned offset)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pipe->screen);
   unsigned num_threads = MAX2(1, screen->num_threads);
   struct llvmpipe_query *pq = llvmpipe_query(q);
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);
   bool unsignalled = false;
   if (pq->fence) {
      /* only have a fence if there was a scene */
      if (!lp_fence_signalled(pq->fence)) {
         if (!lp_fence_issued(pq->fence))
            llvmpipe_flush(pipe, NULL, __FUNCTION__);

         if (wait)
            lp_fence_wait(pq->fence);
      }
      unsignalled = !lp_fence_signalled(pq->fence);
   }


   uint64_t value = 0, value2 = 0;
   unsigned num_values = 1;
   if (index == -1)
      if (unsignalled)
         value = 0;
      else
         value = 1;
   else {
      unsigned i;

      switch (pq->type) {
      case PIPE_QUERY_OCCLUSION_COUNTER:
         for (i = 0; i < num_threads; i++) {
            value += pq->end[i];
         }
         break;
      case PIPE_QUERY_OCCLUSION_PREDICATE:
      case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
         for (i = 0; i < num_threads; i++) {
            /* safer (still not guaranteed) when there's an overflow */
            value = value || pq->end[i];
         }
         break;
      case PIPE_QUERY_PRIMITIVES_GENERATED:
         value = pq->num_primitives_generated[0];
         break;
      case PIPE_QUERY_PRIMITIVES_EMITTED:
         value = pq->num_primitives_written[0];
         break;
      case PIPE_QUERY_TIMESTAMP:
         for (i = 0; i < num_threads; i++) {
            if (pq->end[i] > value) {
               value = pq->end[i];
            }
         }
         break;
      case PIPE_QUERY_TIME_ELAPSED: {
         uint64_t start = (uint64_t)-1, end = 0;
         for (i = 0; i < num_threads; i++) {
            if (pq->start[i] && pq->start[i] < start)
               start = pq->start[i];
            if (pq->end[i] && pq->end[i] > end)
               end = pq->end[i];
         }
         value = end - start;
         break;
      }
      case PIPE_QUERY_SO_STATISTICS:
         value = pq->num_primitives_written[0];
         value2 = pq->num_primitives_generated[0];
         num_values = 2;
         break;
      case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
         value = 0;
         for (unsigned s = 0; s < PIPE_MAX_VERTEX_STREAMS; s++)
            value |= !!(pq->num_primitives_generated[s] > pq->num_primitives_written[s]);
         break;
      case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
         value = !!(pq->num_primitives_generated[0] > pq->num_primitives_written[0]);
         break;
      case PIPE_QUERY_PIPELINE_STATISTICS:
         switch ((enum pipe_statistics_query_index)index) {
         case PIPE_STAT_QUERY_IA_VERTICES:
            value = pq->stats.ia_vertices;
            break;
         case PIPE_STAT_QUERY_IA_PRIMITIVES:
            value = pq->stats.ia_primitives;
            break;
         case PIPE_STAT_QUERY_VS_INVOCATIONS:
            value = pq->stats.vs_invocations;
            break;
         case PIPE_STAT_QUERY_GS_INVOCATIONS:
            value = pq->stats.gs_invocations;
            break;
         case PIPE_STAT_QUERY_GS_PRIMITIVES:
            value = pq->stats.gs_primitives;
            break;
         case PIPE_STAT_QUERY_C_INVOCATIONS:
            value = pq->stats.c_invocations;
            break;
         case PIPE_STAT_QUERY_C_PRIMITIVES:
            value = pq->stats.c_primitives;
            break;
         case PIPE_STAT_QUERY_PS_INVOCATIONS:
            value = 0;
            for (i = 0; i < num_threads; i++) {
               value += pq->end[i];
            }
            value *= LP_RASTER_BLOCK_SIZE * LP_RASTER_BLOCK_SIZE;
            break;
         case PIPE_STAT_QUERY_HS_INVOCATIONS:
            value = pq->stats.hs_invocations;
            break;
         case PIPE_STAT_QUERY_DS_INVOCATIONS:
            value = pq->stats.ds_invocations;
            break;
         case PIPE_STAT_QUERY_CS_INVOCATIONS:
            value = pq->stats.cs_invocations;
            break;
         }
         break;
      default:
         fprintf(stderr, "Unknown query type %d\n", pq->type);
         break;
      }
   }

   void *dst = (uint8_t *)lpr->data + offset;

   for (unsigned i = 0; i < num_values; i++) {

      if (i == 1) {
         value = value2;
         dst = (char *)dst + ((result_type == PIPE_QUERY_TYPE_I64 ||
                               result_type == PIPE_QUERY_TYPE_U64) ? 8 : 4);
      }
      switch (result_type) {
      case PIPE_QUERY_TYPE_I32: {
         int32_t *iptr = (int32_t *)dst;
         if (value > 0x7fffffff)
            *iptr = 0x7fffffff;
         else
            *iptr = (int32_t)value;
         break;
      }
      case PIPE_QUERY_TYPE_U32: {
         uint32_t *uptr = (uint32_t *)dst;
         if (value > 0xffffffff)
            *uptr = 0xffffffff;
         else
            *uptr = (uint32_t)value;
         break;
      }
      case PIPE_QUERY_TYPE_I64: {
         int64_t *iptr = (int64_t *)dst;
         *iptr = (int64_t)value;
         break;
      }
      case PIPE_QUERY_TYPE_U64: {
         uint64_t *uptr = (uint64_t *)dst;
         *uptr = (uint64_t)value;
         break;
      }
      }
   }
}

static bool
llvmpipe_begin_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context( pipe );
   struct llvmpipe_query *pq = llvmpipe_query(q);

   /* Check if the query is already in the scene.  If so, we need to
    * flush the scene now.  Real apps shouldn't re-use a query in a
    * frame of rendering.
    */
   if (pq->fence && !lp_fence_issued(pq->fence)) {
      llvmpipe_finish(pipe, __FUNCTION__);
   }


   memset(pq->start, 0, sizeof(pq->start));
   memset(pq->end, 0, sizeof(pq->end));
   lp_setup_begin_query(llvmpipe->setup, pq);

   switch (pq->type) {
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      pq->num_primitives_written[0] = llvmpipe->so_stats[pq->index].num_primitives_written;
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      pq->num_primitives_generated[0] = llvmpipe->so_stats[pq->index].primitives_storage_needed;
      llvmpipe->active_primgen_queries++;
      break;
   case PIPE_QUERY_SO_STATISTICS:
      pq->num_primitives_written[0] = llvmpipe->so_stats[pq->index].num_primitives_written;
      pq->num_primitives_generated[0] = llvmpipe->so_stats[pq->index].primitives_storage_needed;
      break;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      for (unsigned s = 0; s < PIPE_MAX_VERTEX_STREAMS; s++) {
         pq->num_primitives_written[s] = llvmpipe->so_stats[s].num_primitives_written;
         pq->num_primitives_generated[s] = llvmpipe->so_stats[s].primitives_storage_needed;
      }
      break;
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      pq->num_primitives_written[0] = llvmpipe->so_stats[pq->index].num_primitives_written;
      pq->num_primitives_generated[0] = llvmpipe->so_stats[pq->index].primitives_storage_needed;
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      /* reset our cache */
      if (llvmpipe->active_statistics_queries == 0) {
         memset(&llvmpipe->pipeline_statistics, 0,
                sizeof(llvmpipe->pipeline_statistics));
      }
      memcpy(&pq->stats, &llvmpipe->pipeline_statistics, sizeof(pq->stats));
      llvmpipe->active_statistics_queries++;
      break;
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      llvmpipe->active_occlusion_queries++;
      llvmpipe->dirty |= LP_NEW_OCCLUSION_QUERY;
      break;
   default:
      break;
   }
   return true;
}


static bool
llvmpipe_end_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context( pipe );
   struct llvmpipe_query *pq = llvmpipe_query(q);

   lp_setup_end_query(llvmpipe->setup, pq);

   switch (pq->type) {

   case PIPE_QUERY_PRIMITIVES_EMITTED:
      pq->num_primitives_written[0] =
         llvmpipe->so_stats[pq->index].num_primitives_written - pq->num_primitives_written[0];
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      assert(llvmpipe->active_primgen_queries);
      llvmpipe->active_primgen_queries--;
      pq->num_primitives_generated[0] =
         llvmpipe->so_stats[pq->index].primitives_storage_needed - pq->num_primitives_generated[0];
      break;
   case PIPE_QUERY_SO_STATISTICS:
      pq->num_primitives_written[0] =
         llvmpipe->so_stats[pq->index].num_primitives_written - pq->num_primitives_written[0];
      pq->num_primitives_generated[0] =
         llvmpipe->so_stats[pq->index].primitives_storage_needed - pq->num_primitives_generated[0];
      break;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      for (unsigned s = 0; s < PIPE_MAX_VERTEX_STREAMS; s++) {
         pq->num_primitives_written[s] =
            llvmpipe->so_stats[s].num_primitives_written - pq->num_primitives_written[s];
         pq->num_primitives_generated[s] =
            llvmpipe->so_stats[s].primitives_storage_needed - pq->num_primitives_generated[s];
      }
      break;
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      pq->num_primitives_written[0] =
         llvmpipe->so_stats[pq->index].num_primitives_written - pq->num_primitives_written[0];
      pq->num_primitives_generated[0] =
         llvmpipe->so_stats[pq->index].primitives_storage_needed - pq->num_primitives_generated[0];
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      pq->stats.ia_vertices =
         llvmpipe->pipeline_statistics.ia_vertices - pq->stats.ia_vertices;
      pq->stats.ia_primitives =
         llvmpipe->pipeline_statistics.ia_primitives - pq->stats.ia_primitives;
      pq->stats.vs_invocations =
         llvmpipe->pipeline_statistics.vs_invocations - pq->stats.vs_invocations;
      pq->stats.gs_invocations =
         llvmpipe->pipeline_statistics.gs_invocations - pq->stats.gs_invocations;
      pq->stats.gs_primitives =
         llvmpipe->pipeline_statistics.gs_primitives - pq->stats.gs_primitives;
      pq->stats.c_invocations =
         llvmpipe->pipeline_statistics.c_invocations - pq->stats.c_invocations;
      pq->stats.c_primitives =
         llvmpipe->pipeline_statistics.c_primitives - pq->stats.c_primitives;
      pq->stats.ps_invocations =
         llvmpipe->pipeline_statistics.ps_invocations - pq->stats.ps_invocations;
      pq->stats.cs_invocations =
         llvmpipe->pipeline_statistics.cs_invocations - pq->stats.cs_invocations;
      pq->stats.hs_invocations =
         llvmpipe->pipeline_statistics.hs_invocations - pq->stats.hs_invocations;
      pq->stats.ds_invocations =
         llvmpipe->pipeline_statistics.ds_invocations - pq->stats.ds_invocations;
      llvmpipe->active_statistics_queries--;
      break;
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      assert(llvmpipe->active_occlusion_queries);
      llvmpipe->active_occlusion_queries--;
      llvmpipe->dirty |= LP_NEW_OCCLUSION_QUERY;
      break;
   default:
      break;
   }

   return true;
}

boolean
llvmpipe_check_render_cond(struct llvmpipe_context *lp)
{
   struct pipe_context *pipe = &lp->pipe;
   boolean b, wait;
   uint64_t result;

   if (lp->render_cond_buffer) {
      uint32_t data = *(uint32_t *)((char *)lp->render_cond_buffer->data + lp->render_cond_offset);
      return (!data) == lp->render_cond_cond;
   }
   if (!lp->render_cond_query)
      return TRUE; /* no query predicate, draw normally */

   wait = (lp->render_cond_mode == PIPE_RENDER_COND_WAIT ||
           lp->render_cond_mode == PIPE_RENDER_COND_BY_REGION_WAIT);

   b = pipe->get_query_result(pipe, lp->render_cond_query, wait, (void*)&result);
   if (b)
      return ((!result) == lp->render_cond_cond);
   else
      return TRUE;
}

static void
llvmpipe_set_active_query_state(struct pipe_context *pipe, bool enable)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);

   llvmpipe->queries_disabled = !enable;
   /* for OQs we need to regenerate the fragment shader */
   llvmpipe->dirty |= LP_NEW_OCCLUSION_QUERY;
}

void llvmpipe_init_query_funcs(struct llvmpipe_context *llvmpipe )
{
   llvmpipe->pipe.create_query = llvmpipe_create_query;
   llvmpipe->pipe.destroy_query = llvmpipe_destroy_query;
   llvmpipe->pipe.begin_query = llvmpipe_begin_query;
   llvmpipe->pipe.end_query = llvmpipe_end_query;
   llvmpipe->pipe.get_query_result = llvmpipe_get_query_result;
   llvmpipe->pipe.get_query_result_resource = llvmpipe_get_query_result_resource;
   llvmpipe->pipe.set_active_query_state = llvmpipe_set_active_query_state;
}


